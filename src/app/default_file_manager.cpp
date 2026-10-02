// default_file_manager.cpp — see default_file_manager.h.
#include "default_file_manager.h"

#include "app_prefs.h"
#include "../common/localization.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cwctype>

namespace pulse::app {
namespace {

constexpr wchar_t kThisPcKey[] =
    L"Software\\Classes\\CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}";
constexpr wchar_t kBackupValue[] = L"PulseBackup";
// The replaced handler's DelegateExecute (kept even when empty); restored on turn-off.
constexpr wchar_t kDelegateBackupValue[] = L"PulseBackupDelegateExecute";
constexpr wchar_t kDelegateValue[] = L"DelegateExecute";

std::wstring ModulePath() {
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return n ? std::wstring(path, n) : L"";
}

std::wstring ShellKey() { return std::wstring(kThisPcKey) + L"\\shell"; }
std::wstring OpenKey() { return ShellKey() + L"\\open"; }
std::wstring CommandKey() { return OpenKey() + L"\\command"; }

std::wstring ReadString(const std::wstring& key, const wchar_t* name) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS)
        return {};
    wchar_t value[2048]{};
    DWORD bytes = sizeof(value) - sizeof(wchar_t);
    DWORD type = 0;
    const LONG st = RegQueryValueExW(h, name, nullptr, &type, reinterpret_cast<LPBYTE>(value), &bytes);
    RegCloseKey(h);
    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
    return value;
}

bool WriteString(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &h, nullptr) != ERROR_SUCCESS)
        return false;
    const LONG st = RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return st == ERROR_SUCCESS;
}

bool ValueExists(const std::wstring& key, const wchar_t* name) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS)
        return false;
    const LONG st = RegQueryValueExW(h, name, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(h);
    return st == ERROR_SUCCESS;
}

void DeleteValue(const std::wstring& key, const wchar_t* name) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_SET_VALUE, &h) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(h, name);
    RegCloseKey(h);
}

std::wstring ThisPcCommandLine(const std::wstring& exe) {
    return L"\"" + exe + L"\" \"" + kThisPcParsingName + L"\"";
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() &&
        CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                             static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

} // namespace

DefaultManagerState DefaultFileManagerState(const AppPrefs& prefs) {
    const int on = (prefs.open_folders_in_pulse ? 1 : 0) + (prefs.take_over_win_e ? 1 : 0) +
                   (prefs.take_over_this_pc ? 1 : 0);
    if (on == 0) return DefaultManagerState::Off;
    return on == 3 ? DefaultManagerState::Full : DefaultManagerState::Partial;
}

std::wstring DefaultFileManagerSummary(const AppPrefs& prefs) {
    using I = l10n::StringId;
    if (DefaultFileManagerState(prefs) != DefaultManagerState::Partial)
        return l10n::Get(I::SettingsDefaultManagerDesc);
    std::wstring missing;
    auto add = [&](const std::wstring& part) {
        if (!missing.empty()) missing += l10n::Pick(L"、", L", ");
        missing += part;
    };
    if (!prefs.open_folders_in_pulse) add(l10n::Get(I::SettingsTakeoverFolders));
    if (!prefs.take_over_this_pc) add(l10n::Get(I::ThisPc));
    if (!prefs.take_over_win_e) add(L"Win+E");
    const std::wstring& pattern = l10n::Get(I::SettingsDefaultManagerPartial);
    const size_t at = pattern.find(L"%s");
    if (at == std::wstring::npos) return pattern;
    return pattern.substr(0, at) + missing + pattern.substr(at + 2);
}

bool ApplyDefaultFileManager(AppPrefs& prefs, bool on) {
    bool ok = prefs.ApplyFolderOpen(on);
    ok = prefs.ApplyWinE(on) && ok;
    ok = ApplyThisPcOpen(prefs, on) && ok;
    return ok;
}

bool ReadThisPcOpen(const std::wstring& exe) {
    return FolderOpenCommandIsOurs(ReadString(CommandKey(), nullptr), exe);
}

bool ApplyThisPcOpen(AppPrefs& prefs, bool on) {
    prefs.take_over_this_pc = on;
    if (!prefs.persist) return true;
    const std::wstring exe = ModulePath();
    if (exe.empty()) return false;
    const std::wstring current = ReadString(CommandKey(), nullptr);
    const bool ours = FolderOpenCommandIsOurs(current, exe);
    const std::wstring shell_default = ReadString(ShellKey(), nullptr);
    if (on) {
        const std::wstring line = ThisPcCommandLine(exe);
        if (ours && current == line && EqualsNoCase(shell_default, L"open")) return true;
        bool ok = true;
        // Another file manager's command / default verb comes back on turn-off.
        if (!current.empty() && !ours) ok = WriteString(CommandKey(), kBackupValue, current) && ok;
        if (!ours && ValueExists(CommandKey(), kDelegateValue))
            ok = WriteString(CommandKey(), kDelegateBackupValue, ReadString(CommandKey(), kDelegateValue)) && ok;
        if (!shell_default.empty() && !EqualsNoCase(shell_default, L"open"))
            ok = WriteString(ShellKey(), kBackupValue, shell_default) && ok;
        ok = WriteString(CommandKey(), nullptr, line) && ok;
        // Empty DelegateExecute makes the shell run the command line.
        ok = WriteString(CommandKey(), kDelegateValue, L"") && ok;
        ok = WriteString(ShellKey(), nullptr, L"open") && ok;
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return ok;
    }
    if (!ours) return true;   // someone else's command (or none): leave it alone
    bool ok = true;
    const std::wstring backup = ReadString(CommandKey(), kBackupValue);
    const bool delegate_backup = ValueExists(CommandKey(), kDelegateBackupValue);
    if (!backup.empty() || delegate_backup) {
        // Put the replaced handler back exactly: its command line and its
        // DelegateExecute (or no DelegateExecute when it had none).
        if (!backup.empty()) ok = WriteString(CommandKey(), nullptr, backup) && ok;
        else DeleteValue(CommandKey(), nullptr);
        DeleteValue(CommandKey(), kBackupValue);
        if (delegate_backup) {
            ok = WriteString(CommandKey(), kDelegateValue, ReadString(CommandKey(), kDelegateBackupValue)) && ok;
            DeleteValue(CommandKey(), kDelegateBackupValue);
        } else {
            DeleteValue(CommandKey(), kDelegateValue);
        }
    } else {
        SHDeleteKeyW(HKEY_CURRENT_USER, OpenKey().c_str());
    }
    const std::wstring verb_backup = ReadString(ShellKey(), kBackupValue);
    if (!verb_backup.empty()) {
        ok = WriteString(ShellKey(), nullptr, verb_backup) && ok;
        DeleteValue(ShellKey(), kBackupValue);
    } else if (backup.empty() && EqualsNoCase(shell_default, L"open")) {
        DeleteValue(ShellKey(), nullptr);
    }
    // Drop the now-empty keys so the shell falls back to the HKLM defaults.
    SHDeleteEmptyKeyW(HKEY_CURRENT_USER, ShellKey().c_str());
    SHDeleteEmptyKeyW(HKEY_CURRENT_USER, kThisPcKey);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool IsThisPcArgument(std::wstring_view raw) {
    while (!raw.empty() && (raw.front() == L'"' || iswspace(raw.front()))) raw.remove_prefix(1);
    while (!raw.empty() && (raw.back() == L'"' || raw.back() == L'\\' || iswspace(raw.back())))
        raw.remove_suffix(1);
    constexpr std::wstring_view kShell = L"shell:";
    if (raw.size() > kShell.size() && EqualsNoCase(raw.substr(0, kShell.size()), kShell))
        raw.remove_prefix(kShell.size());
    return EqualsNoCase(raw, kThisPcParsingName) || EqualsNoCase(raw, L"MyComputerFolder");
}

} // namespace pulse::app
