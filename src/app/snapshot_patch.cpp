#include "snapshot_patch.h"
#include "entry_order_hold.h"
#include "../fs/fs_enum.h"
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <optional>
#include <unordered_map>

namespace pulse::app {

namespace {

std::wstring ChildPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L'\\' + name;
}

int FindName(const std::vector<fs::DirEntry>& entries, const std::wstring& name) {
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (_wcsicmp(entries[static_cast<size_t>(i)].name.c_str(), name.c_str()) == 0)
            return i;
    }
    return -1;
}

} // namespace

bool FillDirEntry(const std::wstring& dir, const std::wstring& name, fs::DirEntry& out) {
    if (name.empty() || name.find_first_of(L"\\/") != std::wstring::npos) return false;
    const std::wstring full = fs::NormalizePath(ChildPath(dir, name));
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &data))
        return false;
    out = fs::DirEntry{};
    out.name = name;
    out.attrs = data.dwFileAttributes;
    out.is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out.is_reparse = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    out.cloud_recall = (data.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0;
    out.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    out.mtime = data.ftLastWriteTime;
    out.ctime = data.ftCreationTime;
    out.atime = data.ftLastAccessTime;
    return true;
}

NotifyPatch ApplyDirNotify(std::vector<fs::DirEntry>& entries, const std::wstring& folder,
                           const fs::DirNotifyEvent& event,
                           ui::SortColumn col, ui::SortDirection sort_dir) {
    if (event.name.empty() || event.name.find_first_of(L"\\/") != std::wstring::npos)
        return NotifyPatch::NeedFullEnum;

    if (event.action == FILE_ACTION_REMOVED) {
        const int at = FindName(entries, event.name);
        if (at >= 0) entries.erase(entries.begin() + at);
        return NotifyPatch::Applied;
    }

    if (event.action == FILE_ACTION_RENAMED_NEW_NAME) {
        const int at = FindName(entries, event.old_name.empty() ? event.name : event.old_name);
        fs::DirEntry entry;
        if (!FillDirEntry(folder, event.name, entry)) {
            if (at >= 0) entries.erase(entries.begin() + at);
            return NotifyPatch::Applied;
        }
        // The renamed row keeps its place (#13); a row already holding the
        // new name gives way to it.
        int slot = at;
        const int dup = FindName(entries, event.name);
        if (dup >= 0 && dup != at) {
            if (at >= 0) {
                entries.erase(entries.begin() + dup);
                if (dup < at) --slot;
            } else {
                slot = dup;
            }
        }
        PlaceEntryHeld(entries, slot, std::move(entry), col, sort_dir);
        return NotifyPatch::Applied;
    }

    if (event.action == FILE_ACTION_ADDED || event.action == FILE_ACTION_MODIFIED) {
        fs::DirEntry entry;
        if (!FillDirEntry(folder, event.name, entry)) {
            const int at = FindName(entries, event.name);
            if (at >= 0) entries.erase(entries.begin() + at);
            return NotifyPatch::Applied;
        }
        PlaceEntryHeld(entries, FindName(entries, event.name), std::move(entry), col, sort_dir);
        return NotifyPatch::Applied;
    }

    return NotifyPatch::NeedFullEnum;
}

namespace {

// Case-insensitive name key, folded per character like _wcsicmp/FindName.
struct FoldedNameHash {
    size_t operator()(const std::wstring& name) const noexcept {
        uint64_t hash = 1469598103934665603ull;
        for (wchar_t c : name) {
            hash ^= static_cast<uint64_t>(std::towlower(c));
            hash *= 1099511628211ull;
        }
        return static_cast<size_t>(hash);
    }
};

struct FoldedNameEqual {
    bool operator()(const std::wstring& a, const std::wstring& b) const noexcept {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::towlower(a[i]) != std::towlower(b[i])) return false;
        return true;
    }
};

} // namespace

NotifyPatch ApplyDirNotifyBatch(std::vector<fs::DirEntry>& entries, const std::wstring& folder,
                                const std::vector<fs::DirNotifyEvent>& events,
                                ui::SortColumn col, ui::SortDirection sort_dir) {
    constexpr size_t kSequentialLimit = 4;
    if (events.size() <= kSequentialLimit) {
        for (const auto& event : events) {
            if (ApplyDirNotify(entries, folder, event, col, sort_dir) == NotifyPatch::NeedFullEnum)
                return NotifyPatch::NeedFullEnum;
        }
        return NotifyPatch::Applied;
    }

    for (const auto& event : events) {
        if (event.name.empty() || event.name.find_first_of(L"\\/") != std::wstring::npos)
            return NotifyPatch::NeedFullEnum;
        if (event.action != FILE_ACTION_REMOVED && event.action != FILE_ACTION_RENAMED_NEW_NAME &&
            event.action != FILE_ACTION_ADDED && event.action != FILE_ACTION_MODIFIED)
            return NotifyPatch::NeedFullEnum;
    }

    // Replays the per-event rules on slots instead of a vector: rows keep
    // their slot, and a row that has to move (new, or its group changed) gets
    // a new slot after every original one. Appended slots are merged into
    // their group's end at the finish, exactly where InsertAtGroupEnd would
    // have put them one at a time.
    struct Slot {
        fs::DirEntry entry;
        bool live = true;
    };
    std::vector<Slot> slots;
    slots.reserve(entries.size() + events.size());
    for (auto& entry : entries) slots.push_back({std::move(entry), true});
    const size_t original = slots.size();
    constexpr size_t kNone = static_cast<size_t>(-1);
    std::unordered_map<std::wstring, size_t, FoldedNameHash, FoldedNameEqual> where;
    where.reserve(slots.size() + events.size());
    for (size_t i = 0; i < slots.size(); ++i) where.emplace(slots[i].entry.name, i);
    const auto find = [&](const std::wstring& name) {
        const auto it = where.find(name);
        return it == where.end() ? kNone : it->second;
    };
    const auto drop = [&](size_t slot) {
        if (slot == kNone) return;
        where.erase(slots[slot].entry.name);
        slots[slot].live = false;
    };
    const auto place = [&](size_t slot, fs::DirEntry entry) {
        if (slot != kNone && SameEntryGroup(slots[slot].entry, entry, col, sort_dir)) {
            where.erase(slots[slot].entry.name);
            slots[slot].entry = std::move(entry);
            where[slots[slot].entry.name] = slot;
            return;
        }
        drop(slot);
        slots.push_back({std::move(entry), true});
        where[slots.back().entry.name] = slots.size() - 1;
    };
    // Disk is stat'ed once per spelling; every event for it sees the same state.
    std::unordered_map<std::wstring, std::optional<fs::DirEntry>> stats;
    const auto stat = [&](const std::wstring& name) -> const std::optional<fs::DirEntry>& {
        auto it = stats.find(name);
        if (it == stats.end()) {
            fs::DirEntry entry;
            std::optional<fs::DirEntry> result;
            if (FillDirEntry(folder, name, entry)) result = std::move(entry);
            it = stats.emplace(name, std::move(result)).first;
        }
        return it->second;
    };

    for (const auto& event : events) {
        if (event.action == FILE_ACTION_REMOVED) {
            drop(find(event.name));
            continue;
        }
        if (event.action == FILE_ACTION_RENAMED_NEW_NAME) {
            const size_t at = find(event.old_name.empty() ? event.name : event.old_name);
            const auto& entry = stat(event.name);
            if (!entry) {
                drop(at);
                continue;
            }
            size_t slot = at;
            const size_t dup = find(event.name);
            if (dup != kNone && dup != at) {
                if (at != kNone) drop(dup);
                else slot = dup;
            }
            place(slot, *entry);
            continue;
        }
        const auto& entry = stat(event.name);
        if (!entry) drop(find(event.name));
        else place(find(event.name), *entry);
    }

    std::vector<size_t> appended;
    for (size_t i = original; i < slots.size(); ++i)
        if (slots[i].live) appended.push_back(i);
    std::stable_sort(appended.begin(), appended.end(), [&](size_t a, size_t b) {
        return GroupBefore(slots[a].entry, slots[b].entry, col, sort_dir);
    });
    std::vector<fs::DirEntry> merged;
    merged.reserve(slots.size());
    size_t next = 0;
    for (size_t i = 0; i < original; ++i) {
        if (!slots[i].live) continue;
        while (next < appended.size() &&
               GroupBefore(slots[appended[next]].entry, slots[i].entry, col, sort_dir))
            merged.push_back(std::move(slots[appended[next++]].entry));
        merged.push_back(std::move(slots[i].entry));
    }
    for (; next < appended.size(); ++next) merged.push_back(std::move(slots[appended[next]].entry));
    entries = std::move(merged);
    return NotifyPatch::Applied;
}

} // namespace pulse::app
