// builtin_menu_items.h — Pulse's own context-menu rows and list-row hover
// buttons that the 右键菜单 settings page can hide (#41, #44-⑩).
//
// Open, the cut / copy / delete / rename strip and Properties always stay;
// everything listed here can be turned off. Hidden items persist by key in
// context_menu.json ("pulse_items"), so the enum order is free to change.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace pulse::app {

// Settings-page order.
enum class BuiltinMenuItem : uint8_t {
    OpenInNewTab,
    CopyPath,
    Terminal,
    QuickAccess,
    PinWorkspace,
    PinNetwork,
    Tags,
    RecentChanges,
    SelectCommands,
    Undo,
    RowNewTab,
    RowStar,
    RowMore,
    Count
};

constexpr int kBuiltinMenuItemCount = static_cast<int>(BuiltinMenuItem::Count);

constexpr uint32_t BuiltinMenuBit(BuiltinMenuItem item) {
    return 1u << static_cast<uint32_t>(item);
}

// Stable context_menu.json key ("copy_path", "row_star", ...).
std::wstring_view BuiltinMenuKey(BuiltinMenuItem item);
// Settings-page label in the current UI language.
std::wstring BuiltinMenuLabel(BuiltinMenuItem item);

// List-row hover buttons still shown, as the renderer's mask:
// bit 0 star, bit 1 open in new tab, bit 2 more actions.
constexpr uint32_t kRowActionStar = 1u;
constexpr uint32_t kRowActionNewTab = 2u;
constexpr uint32_t kRowActionMore = 4u;
constexpr uint32_t kRowActionsAll = kRowActionStar | kRowActionNewTab | kRowActionMore;
uint32_t RowActionMask(uint32_t builtin_hidden);

} // namespace pulse::app
