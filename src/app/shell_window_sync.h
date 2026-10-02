// shell_window_sync.h — Keeps ShellWindowRegistry in step with Pulse's panes
// and carries out the shell's "select this item" requests (B站 #1 phase 2a).
#pragma once
#include "shell_window_plan.h"

namespace pulse {
struct AppState;

// After each frame: publishes the panes' folders when they changed. Active
// only while Pulse is the default file manager (not in shot/test runs).
void SyncShellWindows(AppState& s);
// WM_DESTROY: revokes every registration.
void StopShellWindows(AppState& s);
// WM_SHELL_SELECT: brings the pane forward and selects the item.
void HandleShellSelect(AppState& s, const app::ShellSelectRequest& request);

} // namespace pulse
