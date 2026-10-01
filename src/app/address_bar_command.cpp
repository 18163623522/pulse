#include "address_bar_command.h"
#include <cwctype>

namespace pulse::app {
namespace {

bool IsSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == 0x3000;
}

std::wstring_view Trim(std::wstring_view text) {
    while (!text.empty() && IsSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && IsSpace(text.back())) text.remove_suffix(1);
    return text;
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

struct ProgramName {
    const wchar_t* name;
    AddressBarProgram program;
};

constexpr ProgramName kPrograms[] = {
    { L"cmd",        AddressBarProgram::Cmd },
    { L"powershell", AddressBarProgram::PowerShell },
    { L"pwsh",       AddressBarProgram::Pwsh },
    { L"wt",         AddressBarProgram::WindowsTerminal },
};

} // namespace

AddressBarCommand ParseAddressBarCommand(std::wstring_view text) {
    AddressBarCommand result;
    text = Trim(text);
    size_t name_end = 0;
    while (name_end < text.size() && !IsSpace(text[name_end])) ++name_end;
    std::wstring_view name = text.substr(0, name_end);
    if (name.size() > 4 && EqualsNoCase(name.substr(name.size() - 4), L".exe"))
        name.remove_suffix(4);
    for (const auto& candidate : kPrograms) {
        if (EqualsNoCase(name, candidate.name)) {
            result.program = candidate.program;
            result.args = std::wstring(Trim(text.substr(name_end)));
            break;
        }
    }
    return result;
}

const wchar_t* AddressBarProgramExe(AddressBarProgram program) {
    switch (program) {
    case AddressBarProgram::Cmd:             return L"cmd.exe";
    case AddressBarProgram::PowerShell:      return L"powershell.exe";
    case AddressBarProgram::Pwsh:            return L"pwsh.exe";
    case AddressBarProgram::WindowsTerminal: return L"wt.exe";
    case AddressBarProgram::None:            break;
    }
    return nullptr;
}

} // namespace pulse::app
