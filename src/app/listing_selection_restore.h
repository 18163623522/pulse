#pragma once
#include "app_model.h"
#include "../fs/fs_enum.h"
#include <unordered_set>
#include <utility>

namespace pulse::app {
inline void CapturePendingListingSelection(Tab& tab) {
    tab.pending_selection_revision = tab.selection_revision;
    tab.pending_selected_names.clear();
    tab.pending_selected_name.clear();
    tab.pending_ensure_selection_visible = false;
    if (!tab.snapshot || tab.SelectedCount() <= 0) return;
    for (int index : tab.SelectedIndices()) {
        if (index >= 0 && index < static_cast<int>(tab.EntryCount()))
            tab.pending_selected_names.push_back(tab.EntryAt(static_cast<size_t>(index)).name);
    }
    if (tab.selected_index >= 0 && tab.selected_index < static_cast<int>(tab.EntryCount()))
        tab.pending_selected_name = tab.EntryAt(static_cast<size_t>(tab.selected_index)).name;
}

struct ListingSelectionRestore {
    std::vector<std::wstring> names;
    std::wstring focus;
    bool ensure_visible = false;
    bool user_changed = false;
};

// Called after request-generation validation and before replacing the old snapshot.
inline ListingSelectionRestore TakeListingSelection(Tab& tab, const std::wstring& result_path) {
    const bool changed = tab.pending_selection_revision != UINT64_MAX &&
        tab.pending_selection_revision != tab.selection_revision && tab.snapshot &&
        tab.snapshot_path == result_path && !fs::IsVirtualPath(result_path);
    if (changed) CapturePendingListingSelection(tab);
    ListingSelectionRestore restore{
        std::exchange(tab.pending_selected_names, {}),
        std::exchange(tab.pending_selected_name, {}),
        std::exchange(tab.pending_ensure_selection_visible, false), changed};
    tab.pending_selection_revision = UINT64_MAX;
    return restore;
}

inline void RestoreListingSelection(Tab& tab, const std::vector<std::wstring>& names,
                                    const std::wstring& focus, bool user_changed) {
    if (user_changed) {
        const std::unordered_set<std::wstring> wanted(names.begin(), names.end());
        bool survives = false;
        for (size_t i = 0; i < tab.EntryCount(); ++i) {
            if (!tab.EntryVisible(static_cast<int>(i))) continue;
            const auto name = tab.EntryAt(i).name;
            if (wanted.contains(name)) {
                survives = true;
                break;
            }
        }
        if (!survives) { tab.ClearSelection(); return; }
    }
    if (!names.empty()) tab.RemapSelection(names, focus);
    else if (tab.snapshot && tab.EntryCount() != 0) tab.SelectOnly(0);
    else tab.ClearSelection();
}
} // namespace pulse::app
