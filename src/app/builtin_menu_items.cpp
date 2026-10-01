#include "builtin_menu_items.h"

#include "../common/localization.h"

namespace pulse::app {
namespace {

struct BuiltinMenuInfo {
    BuiltinMenuItem item;
    const wchar_t* key;
    l10n::StringId label;
};

constexpr BuiltinMenuInfo kItems[] = {
    { BuiltinMenuItem::OpenInNewTab,   L"open_new_tab",   l10n::StringId::OpenNewTab },
    { BuiltinMenuItem::CopyPath,       L"copy_path",      l10n::StringId::CopyPath },
    { BuiltinMenuItem::Terminal,       L"terminal",       l10n::StringId::OpenTerminalHere },
    { BuiltinMenuItem::QuickAccess,    L"quick_access",   l10n::StringId::PinQuickAccess },
    { BuiltinMenuItem::PinWorkspace,   L"pin_workspace",  l10n::StringId::PinWorkspace },
    { BuiltinMenuItem::PinNetwork,     L"pin_network",    l10n::StringId::PinNetwork },
    { BuiltinMenuItem::Tags,           L"tags",           l10n::StringId::ContextBuiltinTags },
    { BuiltinMenuItem::RecentChanges,  L"recent_changes", l10n::StringId::ChangeView },
    { BuiltinMenuItem::SelectCommands, L"select",         l10n::StringId::ContextBuiltinSelect },
    { BuiltinMenuItem::Undo,           L"undo",           l10n::StringId::Undo },
    { BuiltinMenuItem::RowNewTab,      L"row_new_tab",    l10n::StringId::ContextRowNewTab },
    { BuiltinMenuItem::RowStar,        L"row_star",       l10n::StringId::ContextRowStar },
    { BuiltinMenuItem::RowMore,        L"row_more",       l10n::StringId::ContextRowMore },
};
static_assert(sizeof(kItems) / sizeof(kItems[0]) == kBuiltinMenuItemCount,
              "every built-in item needs a key and a label");

constexpr bool ItemsInEnumOrder() {
    for (size_t i = 0; i < sizeof(kItems) / sizeof(kItems[0]); ++i)
        if (static_cast<size_t>(kItems[i].item) != i) return false;
    return true;
}
static_assert(ItemsInEnumOrder(), "kItems is indexed by BuiltinMenuItem");

const BuiltinMenuInfo& Info(BuiltinMenuItem item) {
    return kItems[static_cast<size_t>(item)];
}

} // namespace

std::wstring_view BuiltinMenuKey(BuiltinMenuItem item) {
    return Info(item).key;
}

std::wstring BuiltinMenuLabel(BuiltinMenuItem item) {
    return l10n::Get(Info(item).label);
}

uint32_t RowActionMask(uint32_t builtin_hidden) {
    uint32_t mask = kRowActionsAll;
    if (builtin_hidden & BuiltinMenuBit(BuiltinMenuItem::RowStar)) mask &= ~kRowActionStar;
    if (builtin_hidden & BuiltinMenuBit(BuiltinMenuItem::RowNewTab)) mask &= ~kRowActionNewTab;
    if (builtin_hidden & BuiltinMenuBit(BuiltinMenuItem::RowMore)) mask &= ~kRowActionMore;
    return mask;
}

} // namespace pulse::app
