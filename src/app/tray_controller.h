#pragma once

#include <windows.h>
#include <shellapi.h>

#include <functional>

namespace pulse::app {

class TrayController {
public:
    enum class CallbackResult { NotHandled, Handled, ExitRequested };
    static constexpr UINT kCallbackMessage = WM_APP + 50;

    TrayController() = default;
    ~TrayController();
    TrayController(const TrayController&) = delete;
    TrayController& operator=(const TrayController&) = delete;

    void Attach(HWND hwnd, HINSTANCE instance);
    void Detach();
    bool SetVisible(bool visible);
    void HideWindow();
    void RestoreWindow();
    // Sign-in launch into the tray: add the icon and keep the window hidden.
    // Returns false (caller shows the window) when the icon cannot be added
    // although the taskbar exists; before Explorer is up the icon is added
    // on TaskbarCreated. `maximized`: the first restore shows it maximized.
    bool StartHidden(bool maximized);
    // Explorer (re)created the taskbar: icons added earlier are gone.
    static UINT TaskbarCreatedMessage();
    void HandleTaskbarCreated();
    bool IconVisible() const { return icon_added_; }
    // Runs inside RestoreWindow() just before a hidden window is shown again
    // (tray click, tray "Open", a second launch), so the app can reset it first.
    void SetBeforeRestore(std::function<void()> hook) { before_restore_ = std::move(hook); }
    CallbackResult HandleCallback(LPARAM event);

    bool IsVisible() const noexcept { return icon_added_; }

private:
    NOTIFYICONDATAW IconData(UINT flags = 0) const;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    bool icon_added_ = false;
    bool wanted_visible_ = false;   // last SetVisible request, for TaskbarCreated
    bool restore_maximized_ = false;
    std::function<void()> before_restore_;
};

} // namespace pulse::app
