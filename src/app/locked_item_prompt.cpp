// locked_item_prompt.cpp — see locked_item_prompt.h.
#include "locked_item_prompt.h"
#include "app_internal.h"
#include "../common/localization.h"
#include "../ui/file_operation_dialog.h"
#include <algorithm>

namespace pulse {

bool LockedItemOwnersClosable(const ops::OpStatus& status) {
    return !status.lock_owners.empty() &&
        std::all_of(status.lock_owners.begin(), status.lock_owners.end(),
                    [](const ops::LockOwner& owner) { return owner.closable; });
}

std::wstring LockedItemPromptMessage(const ops::OpStatus& status, bool can_end) {
    std::wstring name = status.locked_path;
    const size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash + 1 < name.size()) name.erase(0, slash + 1);
    std::wstring message = l10n::Get(l10n::StringId::LockedItemMessage);
    const size_t marker = message.find(L"{name}");
    if (marker != std::wstring::npos) message.replace(marker, 6, name);
    message += L"\n\n";
    message += ops::FormatLockOwnerLines(status.lock_owners);
    message += L"\n\n";
    message += l10n::Get(can_end ? l10n::StringId::LockedItemEndHint
                                 : l10n::StringId::LockedItemCloseHint);
    return message;
}

void PromptLockedItem(AppState& s, const ops::OpStatus& status) {
    const bool can_end = LockedItemOwnersClosable(status);
    ui::ConfirmDialogSpec spec;
    spec.title = l10n::Get(l10n::StringId::LockedItemTitle);
    spec.message = LockedItemPromptMessage(status, can_end);
    spec.confirm_text = l10n::Get(can_end ? l10n::StringId::LockedItemEndRetry
                                          : l10n::StringId::LockedItemRetry);
    spec.cancel_text = l10n::Get(l10n::StringId::Cancel);
    spec.danger = can_end;
    if (!ui::ShowConfirmDialog(s.hwnd, spec, s.darkMode, s.accentColor)) return;
    s.ops.RetryLockedOperation(status.task_id, can_end);
}

} // namespace pulse
