// address_bar_command.h — Typing cmd / powershell / pwsh / wt in the address
// bar opens that program in the current folder, like Explorer (#44-⑤).
#pragma once
#include <string>
#include <string_view>

namespace pulse::app {

enum class AddressBarProgram { None, Cmd, PowerShell, Pwsh, WindowsTerminal };

struct AddressBarCommand {
    AddressBarProgram program = AddressBarProgram::None;
    std::wstring args;   // Everything after the program name, trimmed.
};

// Recognizes "<program>[.exe] [args]" (case-insensitive). Anything else,
// including paths, returns AddressBarProgram::None and is navigated as before.
AddressBarCommand ParseAddressBarCommand(std::wstring_view text);

// Executable to launch for a recognized program (nullptr for None).
const wchar_t* AddressBarProgramExe(AddressBarProgram program);

} // namespace pulse::app
