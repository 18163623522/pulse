// tray_reveal.cpp — Closing to the tray and coming back (see tray_reveal.h).
#include "tray_reveal.h"
#include "app_internal.h"
#include "startup_location.h"

namespace pulse {

void HideMainWindowToTray(AppState& s) {
    s.hidden_to_tray = true;
    s.tray_controller.HideWindow();
}

bool TakeFreshStart(AppState& s) {
    const bool hidden = s.hidden_to_tray;
    s.hidden_to_tray = false;
    return hidden && !app::RestoresLastTabs(s.appPrefs);
}

void StartFreshAt(AppState& s, const std::wstring& path) {
    if (s.renameIndex >= 0) HideRenameOverlay(s, false);
    if (s.addressEditing) HideAddressEditor(s, false);
    OpenTabAt(s, path);
    // Highest index first; WindowTabs::CloseTab keeps pinned tabs and fixes
    // up the active index when a tab before it goes away.
    for (size_t i = s.window_tabs.items.size(); i-- > 0;) {
        if (i != s.window_tabs.active) CloseLayoutTab(s, i);
    }
}

void InstallTrayRevealHook(AppState& s) {
    s.tray_controller.SetBeforeRestore([&s] {
        if (TakeFreshStart(s)) StartFreshAt(s, app::DefaultLocation(s.appPrefs));
    });
}

} // namespace pulse
