// folder_listing.cpp — see folder_listing.h.
#include "folder_listing.h"
#include "../ipc/preview_protocol.h"
#include <shlwapi.h>
#include <algorithm>
#include <cwctype>
#include <unordered_map>
#include <vector>

namespace pulse::preview {
namespace {

constexpr uint32_t kMaxEntries = 200000;
constexpr int kMaxDepth = 64;
constexpr size_t kMaxRows = 4000;
constexpr size_t kMaxExtensions = 40;

struct Entry {
    std::wstring name;
    uint64_t size = 0;
    FILETIME modified{};
    int first_kid = -1;
    int kid_count = 0;
    bool dir = false;
    bool skip = false;  // junction / link / cloud placeholder: listed, not entered
};

struct Walk {
    std::vector<Entry> nodes;
    std::unordered_map<std::wstring, uint64_t> extensions;
    uint64_t files = 0, dirs = 0, total = 0;
    uint32_t seen = 0;
    ULONGLONG deadline = 0;
    bool timed_out = false;
    bool capped = false;

    bool Stop() {
        if (capped || timed_out) return true;
        if (seen >= kMaxEntries) capped = true;
        else if (GetTickCount64() >= deadline) timed_out = true;
        return capped || timed_out;
    }
};

bool IsPlaceholder(DWORD attrs) {
    return (attrs & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS |
                     FILE_ATTRIBUTE_RECALL_ON_OPEN)) != 0;
}

std::wstring ExtensionKey(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0 || name.size() - dot - 1 > 5) return {};
    std::wstring ext = name.substr(dot + 1);
    for (wchar_t& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    return ext;
}

// Lists one folder into a contiguous run of nodes, then descends.
void Scan(Walk& w, int index, const std::wstring& dir_path, int depth) {
    if (w.Stop()) return;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileExW((dir_path + L"\\*").c_str(), FindExInfoBasic, &fd,
                                   FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) return;
    std::vector<Entry> kids;
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
            continue;
        Entry e;
        e.name = fd.cFileName;
        e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.modified = fd.ftLastWriteTime;
        e.skip = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                 IsPlaceholder(fd.dwFileAttributes);
        if (!e.dir) {
            e.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            ++w.files;
            w.total += e.size;
            w.extensions[ExtensionKey(e.name)] += e.size;
        } else {
            ++w.dirs;
        }
        kids.push_back(std::move(e));
        if (++w.seen >= kMaxEntries) { w.capped = true; break; }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(kids.begin(), kids.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
    });
    const int first = static_cast<int>(w.nodes.size());
    const int count = static_cast<int>(kids.size());
    for (Entry& kid : kids) w.nodes.push_back(std::move(kid));
    w.nodes[index].first_kid = first;
    w.nodes[index].kid_count = count;
    for (int i = 0; i < count; ++i) {
        const Entry& kid = w.nodes[first + i];
        if (!kid.dir || kid.skip) continue;
        if (depth >= kMaxDepth) { w.capped = true; continue; }
        if (w.Stop()) break;
        Scan(w, first + i, dir_path + L"\\" + kid.name, depth + 1);
    }
    uint64_t size = 0;
    for (int i = 0; i < count; ++i) size += w.nodes[first + i].size;
    w.nodes[index].size = size;
}

void AppendDate(std::wstring& text, const FILETIME& time) {
    SYSTEMTIME utc{}, local{};
    if ((!time.dwLowDateTime && !time.dwHighDateTime) || !FileTimeToSystemTime(&time, &utc) ||
        !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        text += L'-';
        return;
    }
    wchar_t buf[32]{};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u", local.wYear, local.wMonth, local.wDay,
               local.wHour, local.wMinute);
    text += buf;
}

void Emit(const Walk& w, const std::vector<char>& included, int index, int depth,
          std::wstring& text) {
    const Entry& parent = w.nodes[index];
    for (int i = 0; i < parent.kid_count; ++i) {
        if (text.size() > ipc::kPreviewMaxArchiveChars - 1024) return;
        const int k = parent.first_kid + i;
        const Entry& e = w.nodes[k];
        text += std::to_wstring(depth);
        text += e.dir ? L"\td\t" : L"\t-\t";
        text += std::to_wstring(e.size);
        text += L'\t';
        text += e.dir ? std::to_wstring(e.kid_count) : std::wstring(L"0");
        text += L'\t';
        AppendDate(text, e.modified);
        text += L'\t';
        text += e.name;
        text += L'\n';
        if (e.dir && included[k]) Emit(w, included, k, depth + 1, text);
    }
}

} // namespace

bool MakeFolderListing(const std::wstring& path, uint32_t budget_ms, bool final_pass,
                       std::wstring& text) {
    std::wstring root = path;
    while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
    if (root.size() == 3 && root[1] == L':') root.pop_back();  // "C:\" -> "C:"
    std::wstring walk_root = root;
    if (walk_root.rfind(L"\\\\?\\", 0) != 0) {
        if (walk_root.rfind(L"\\\\", 0) == 0) walk_root = L"\\\\?\\UNC\\" + walk_root.substr(2);
        else if (walk_root.size() >= 2 && walk_root[1] == L':') walk_root = L"\\\\?\\" + walk_root;
    }
    {
        WIN32_FIND_DATAW probe{};
        HANDLE find = FindFirstFileExW((walk_root + L"\\*").c_str(), FindExInfoBasic, &probe,
                                       FindExSearchNameMatch, nullptr, 0);
        if (find == INVALID_HANDLE_VALUE) return false;
        FindClose(find);
    }

    Walk w;
    w.deadline = GetTickCount64() + budget_ms;
    w.nodes.reserve(4096);
    w.nodes.push_back(Entry{});
    w.nodes[0].dir = true;
    Scan(w, 0, walk_root, 0);

    // Rows: whole levels while they fit, so the top of the tree is complete.
    std::vector<char> included(w.nodes.size(), 0);
    included[0] = 1;
    size_t rows = static_cast<size_t>(w.nodes[0].kid_count);
    std::vector<int> level{0};
    while (!level.empty() && rows < kMaxRows) {
        std::vector<int> next;
        for (int parent : level) {
            const Entry& p = w.nodes[parent];
            for (int i = 0; i < p.kid_count; ++i) {
                const int k = p.first_kid + i;
                const Entry& kid = w.nodes[k];
                if (!kid.dir || kid.first_kid < 0 || kid.kid_count == 0) continue;
                if (rows + static_cast<size_t>(kid.kid_count) > kMaxRows) continue;
                rows += static_cast<size_t>(kid.kid_count);
                included[k] = 1;
                next.push_back(k);
            }
        }
        level.swap(next);
    }

    const wchar_t* state = w.capped ? L"1" : w.timed_out ? (final_pass ? L"1" : L"2") : L"0";
    text = L"PULSEARC\t1\tDIR\t-\t";
    text += state;
    text += L"\n#S\t";
    text += std::to_wstring(w.files);
    text += L'\t';
    text += std::to_wstring(w.dirs);
    text += L'\t';
    text += std::to_wstring(w.total);
    text += L"\t-\t";
    std::vector<std::pair<uint64_t, std::wstring>> by_size;
    by_size.reserve(w.extensions.size());
    for (const auto& [ext, bytes] : w.extensions) by_size.push_back({bytes, ext});
    std::sort(by_size.begin(), by_size.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    uint64_t rest = 0;
    for (size_t i = 0; i < by_size.size(); ++i) {
        if (i >= kMaxExtensions || by_size[i].second.empty()) { rest += by_size[i].first; continue; }
        text += by_size[i].second + L':' + std::to_wstring(by_size[i].first) + L'|';
    }
    if (rest) text += L":" + std::to_wstring(rest) + L'|';
    text += L'\n';
    Emit(w, included, 0, 0, text);
    return true;
}

} // namespace pulse::preview
