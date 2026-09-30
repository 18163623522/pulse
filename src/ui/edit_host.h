#pragma once
#include <windows.h>
#include "ui_compositor.h"

namespace pulse::ui {
bool HandleChildEditMessage(Compositor& compositor, IDWriteTextFormat* format,
    D2D1_COLOR_F foreground, D2D1_COLOR_F background, HBRUSH background_brush,
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result);
// Presents a child editor's LumaText bitmap (e.g. right after showing it).
// When the layered present fails, the EDIT switches to native painting so it
// stays visible and clickable. Returns true when the LumaText bitmap is shown.
bool PresentChildEdit(Compositor& compositor, IDWriteTextFormat* format,
    D2D1_COLOR_F foreground, D2D1_COLOR_F background, HWND hwnd);
LRESULT DefPresentedChildEditProc(Compositor& compositor, IDWriteTextFormat* format,
    D2D1_COLOR_F foreground, D2D1_COLOR_F background,
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
// EDIT owns native editing and IME. Its LumaText bitmap is a child surface,
// clipped and moved by the parent rather than an independently owned popup.
inline HWND CreateChildEdit(HWND parent, const wchar_t* text = L"", DWORD edit_style = 0) {
    return CreateWindowExW(WS_EX_LAYERED, L"EDIT", text,
        WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL | edit_style,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}
}
