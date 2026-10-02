// shell_window_sync.cpp — see shell_window_sync.h.
#include "shell_window_sync.h"

#include "app_input.h"
#include "app_navigation.h"
#include "app_runtime.h"
#include "app_state.h"
#include "../common/path_utils.h"

#include <shlobj.h>

namespace pulse {
namespace {

bool ShellWindowsEnabled(const AppState& s) {
    if (s.shot.active || s.menushot) return false;
    if (s.isolatedTest) {
#ifdef PULSE_WITH_SELFTEST
        // Real-shell test of an isolated instance; it never touches the
        // user's Pulse, but it does appear in IShellWindows.
        static const bool forced = GetEnvironmentVariableW(L"PULSE_TEST_SHELL_WINDOWS", nullptr, 0) > 0;
        return forced;
#else
        return false;
#endif
    }
    return s.appPrefs.open_folders_in_pulse;
}

uint64_t PaneKey(const app::Pane& pane) { return reinterpret_cast<uint64_t>(&pane); }

bool SameFolder(const std::wstring& a, const std::wstring& b) {
    return path::EqualInsensitive(path::StripExtendedPathPrefix(fs::NormalizePath(a)),
                                  path::StripExtendedPathPrefix(fs::NormalizePath(b)));
}

// Adds `name` to the selection instead of replacing it: the shell sends one
// SelectItem per item when a program selects several files.
void AddNameToSelection(AppState& s, app::Tab& tab, const std::wstring& name) {
    tab.pending_selected_names.push_back(name);
    tab.pending_selected_name = name;
    tab.pending_ensure_selection_visible = true;
    if (!tab.snapshot) return;
    const auto& entries = *tab.snapshot;
    for (size_t i = 0; i < entries.size(); ++i) {
        const int index = static_cast<int>(i);
        if (_wcsicmp(entries[i].name.c_str(), name.c_str()) != 0 || !tab.EntryVisible(index)) continue;
        if (!tab.IsSelected(index)) tab.ToggleSelect(index);
        EnsureRowVisible(s, tab, index);
        break;
    }
}

} // namespace

void SyncShellWindows(AppState& s) {
    std::vector<app::ShellWindowEntry> wanted;
    if (ShellWindowsEnabled(s)) {
        ForEachPane(s, [&](app::Pane& pane) {
            const app::Tab* tab = pane.ActiveTab();
            if (!tab) return;
            // The fs layer's \\?\ paths mean nothing to the shell.
            std::wstring folder = path::StripExtendedPathPrefix(tab->current_path);
            if (app::IsShellWindowPath(folder)) wanted.push_back({PaneKey(pane), std::move(folder)});
        });
    }
    if (wanted == s.shell_windows_published) return;
    app::TraceShellWindows(L"sync enabled=%d wanted=%zu first=%s", ShellWindowsEnabled(s) ? 1 : 0,
                           wanted.size(), wanted.empty() ? L"" : wanted.front().path.c_str());
    if (!s.shell_windows) {
        if (wanted.empty()) return;
        s.shell_windows = std::make_unique<app::ShellWindowRegistry>(s.hwnd, WM_SHELL_SELECT);
    }
    s.shell_windows->Publish(wanted);
    s.shell_windows_published = std::move(wanted);
}

void StopShellWindows(AppState& s) {
    if (s.shell_windows) s.shell_windows->Stop();
    s.shell_windows.reset();
    s.shell_windows_published.clear();
}

void HandleShellSelect(AppState& s, const app::ShellSelectRequest& request) {
    size_t layout_index = 0;
    app::Pane* target = nullptr;
    for (size_t i = 0; i < s.window_tabs.items.size() && !target; ++i) {
        if (!s.window_tabs.items[i]) continue;
        for (auto& pane : s.window_tabs.items[i]->panes) {
            if (pane && PaneKey(*pane) == request.key) {
                layout_index = i;
                target = pane.get();
                break;
            }
        }
    }
    app::TraceShellWindows(L"select request key=%llx path=%s flags=0x%x found=%d",
                           static_cast<unsigned long long>(request.key), request.path.c_str(), request.flags,
                           target ? 1 : 0);
    if (!target) return;   // pane closed after the shell looked it up

    s.tray_controller.RestoreWindow();
    if (layout_index != s.window_tabs.active) SwitchTab(s, layout_index);
    FocusPane(s, target);
    app::Tab* tab = ActiveTab(s);
    if (!tab) return;

    std::wstring folder;
    std::wstring leaf;
    // This PC (drives) or a share root: showing the pane is all there is.
    if (!app::SplitShellItemPath(request.path, folder, leaf) || tab->current_path.empty()) {
        InvalidateRect(s.hwnd, nullptr, FALSE);
        return;
    }
    if (!SameFolder(folder, tab->current_path)) {
        NavigateTo(s, folder);
        tab = ActiveTab(s);
        if (!tab) return;
    }
    // SVSI_EDIT (rename) still only selects: Pulse never starts a rename
    // on another program's behalf.
    if (request.flags & SVSI_DESELECTOTHERS) SelectNameInTab(s, *tab, leaf);
    else AddNameToSelection(s, *tab, leaf);
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

} // namespace pulse
