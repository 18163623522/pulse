#include "../index/index_directory_security.h"
#include "../index/index_migration.h"
#include "../index/index_config.h"
#include "../index/network_agent_security.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>

using namespace pulse::index;
namespace fs = std::filesystem;
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failures;
    };
    const auto owner = agent::ProcessIdentity();
    check(owner.valid(), "process user, logon SID and session available");
    agent::Security security(owner);
    check(security.descriptor != nullptr && !agent::PipeName().empty(), "scoped endpoint security constructed");
    const auto name = agent::PipeName() + L"-test-" + std::to_wstring(GetCurrentProcessId());
    HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1024, 1024, 0, &security.attributes);
    check(pipe != INVALID_HANDLE_VALUE, "isolated pipe created with production ACL");
    if (pipe != INVALID_HANDLE_VALUE) {
        HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        check(client != INVALID_HANDLE_VALUE, "same logon client can connect");
        if (client != INVALID_HANDLE_VALUE) {
            ConnectNamedPipe(pipe, nullptr);
            DWORD bytes = 0; char value = 'x';
            WriteFile(client, &value, 1, &bytes, nullptr);
            ReadFile(pipe, &value, 1, &bytes, nullptr);
            check(agent::AuthorizeServer(client), "client verifies actual server process token");
            check(agent::AuthorizeClient(pipe, owner), "server authorizes actual client token");
            auto wrong = owner; ++wrong.session;
            check(!agent::AuthorizeClient(pipe, wrong), "different session denied before dispatch");
            wrong = owner; wrong.user += L"-1";
            check(!agent::AuthorizeClient(pipe, wrong), "different user denied before dispatch");
            wrong = owner; wrong.logon += L"-1";
            check(!agent::AuthorizeClient(pipe, wrong), "different logon denied before dispatch");
            CloseHandle(client); DisconnectNamedPipe(pipe); CloseHandle(pipe);
            pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1024, 1024, 0, &security.attributes);
        }
        HANDLE other = nullptr;
        // NEW_CREDENTIALS retains local groups but creates a different authentication
        // session. No server or account is touched and the credentials are never used.
        const BOOL logged = LogonUserW(L"PulseIsolationFixture", L".", L"unused-fixture-password",
            LOGON32_LOGON_NEW_CREDENTIALS, LOGON32_PROVIDER_WINNT50, &other);
        check(logged != FALSE, "isolated second logon token created");
        if (logged) {
            const auto alternate = agent::TokenIdentity(other);
            check(alternate.valid() && alternate.authentication != owner.authentication,
                "new credentials token has distinct real authentication session");
            if (ImpersonateLoggedOnUser(other)) {
                HANDLE other_client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                    OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
                const DWORD connection_error = other_client == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
                RevertToSelf();
                printf("second_credentials_connect_error=%lu\n", connection_error);
                check(other_client != INVALID_HANDLE_VALUE, "same local logon group reaches authorization gate");
                if (other_client != INVALID_HANDLE_VALUE) {
                    ConnectNamedPipe(pipe, nullptr);
                    DWORD bytes = 0;
                    bool rejected = true;
                    for (char request = 1; request <= 9; ++request) {
                        char read = 0;
                        WriteFile(other_client, &request, 1, &bytes, nullptr);
                        ReadFile(pipe, &read, 1, &bytes, nullptr);
                        rejected = !agent::AuthorizeClient(pipe, owner) && rejected;
                    }
                    check(rejected, "actual foreign authentication session rejected for all request kinds");
                    CloseHandle(other_client); DisconnectNamedPipe(pipe); CloseHandle(pipe);
                    pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1024, 1024, 0, &security.attributes);
                }
            } else check(false, "second logon impersonation");
            CloseHandle(other);
        }
        HANDLE primary = nullptr, no_logon = nullptr;
        PSID logon_sid = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &primary) &&
            ConvertStringSidToSidW(owner.logon.c_str(), &logon_sid)) {
            SID_AND_ATTRIBUTES disabled{logon_sid, 0};
            if (CreateRestrictedToken(primary, DISABLE_MAX_PRIVILEGE, 1, &disabled, 0, nullptr, 0, nullptr, &no_logon) &&
                ImpersonateLoggedOnUser(no_logon)) {
                HANDLE denied = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                    OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
                const DWORD code = GetLastError(); RevertToSelf();
                check(denied == INVALID_HANDLE_VALUE && code == ERROR_ACCESS_DENIED,
                    "actual token without enabled owner logon SID denied by pipe ACL");
                if (denied != INVALID_HANDLE_VALUE) CloseHandle(denied);
            } else check(false, "restricted logon token creation");
        } else check(false, "restricted logon token setup");
        if (no_logon) CloseHandle(no_logon);
        if (primary) CloseHandle(primary);
        if (logon_sid) LocalFree(logon_sid);
        CloseHandle(pipe);
    }
    HANDLE token = nullptr, restricted = nullptr;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token);
    check(token && CreateRestrictedToken(token, DISABLE_MAX_PRIVILEGE | LUA_TOKEN, 0, nullptr,
        0, nullptr, 0, nullptr, &restricted), "restricted ordinary-user token created");
    if (token) CloseHandle(token);
    const auto root = fs::absolute(fs::path("bench_data") / ("index-security-" + std::to_string(GetCurrentProcessId())));
    fs::create_directories(root);
    auto write = [](const fs::path& path) { fs::create_directories(path.parent_path()); std::ofstream(path) << "private-index-metadata"; };
    const auto source = root / L"source", target = root / L"target", unsafe = root / L"existing";
    const auto pinned = root / L"pinned";
    fs::create_directories(pinned);
    {
        PrivateIndexDirectoryLock pin;
        check(pin.Acquire(pinned), "migration path components pinned without delete sharing");
        const BOOL renamed = MoveFileExW(pinned.c_str(), (root / L"renamed").c_str(), 0);
        const DWORD leaf_error = renamed ? ERROR_SUCCESS : GetLastError();
        printf("pinned_leaf_rename_error=%lu\n", leaf_error);
        check(!renamed && (leaf_error == ERROR_SHARING_VIOLATION || leaf_error == ERROR_ACCESS_DENIED) &&
            fs::exists(pinned) && !fs::exists(root / L"renamed"),
            "actual leaf rename refused while migration owns path lock");
        const BOOL parent_renamed = MoveFileExW(root.c_str(), (root.wstring() + L"-renamed").c_str(), 0);
        const DWORD parent_error = parent_renamed ? ERROR_SUCCESS : GetLastError();
        printf("pinned_parent_rename_error=%lu\n", parent_error);
        check(!parent_renamed && (parent_error == ERROR_SHARING_VIOLATION || parent_error == ERROR_ACCESS_DENIED) &&
            fs::exists(root) && !fs::exists(root.wstring() + L"-renamed"),
            "actual ancestor rename refused while migration owns path lock");
    }
    write(source / L"pulse-index.bin"); write(unsafe / L"pulse-index.bin");
    IndexMigration migration; std::wstring error;
    check(!CopyIndexForMigration(source.wstring(), unsafe.wstring(), migration, error, true) &&
        fs::exists(source / L"pulse-index.bin") && fs::exists(unsafe / L"pulse-index.bin"),
        "readable nonempty matching target rejected without deleting either copy");
    // An elevated preparation failure is a regression, not an environment skip.
    HANDLE process_token = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD elevation_bytes = 0;
    const bool have_elevation = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token) &&
        GetTokenInformation(process_token, TokenElevation, &elevation, sizeof(elevation), &elevation_bytes);
    if (process_token) CloseHandle(process_token);
    check(have_elevation, "process elevation state is known before classifying private-directory failures");
    const bool prepared = PreparePrivateIndexDirectory(target);
    check(prepared || (have_elevation && !elevation.TokenIsElevated),
        "elevated process must prepare and validate an empty private directory");
    if (!prepared) {
        check(!CopyIndexForMigration(source.wstring(), target.wstring(), migration, error, true) &&
            fs::exists(source / L"pulse-index.bin") && !fs::exists(target / L"pulse-index.bin"),
            "unprivileged migration fails closed before copying and retains source");
        printf("[SKIP] successful private migration and restricted-token reads require elevated administrator\n");
    }
    if (prepared) {
        check(IsPrivateIndexObject(target, true),
            "production private-object check reads attributes and security on the same handle");
        check(ProtectIndexDirectory(target.wstring()) && ProtectIndexDirectory(target.wstring()),
            "same-path empty private directory protection succeeds repeatedly");
        const auto existing_artifact = target / L"pulse-index.bin";
        write(existing_artifact);
        check(IsPrivateIndexObject(existing_artifact) && ProtectIndexDirectory(target.wstring()),
            "same-path nonempty private index validates inherited file security without migration");
        {
            std::ifstream preserved(existing_artifact, std::ios::binary);
            const std::string bytes{std::istreambuf_iterator<char>(preserved), std::istreambuf_iterator<char>()};
            check(bytes == "private-index-metadata", "same-path protection retains existing index bytes");
        }
        fs::remove(existing_artifact);
        auto denied_read = [&](const fs::path& path) {
            if (!restricted || !ImpersonateLoggedOnUser(restricted)) return false;
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            const auto code = GetLastError(); RevertToSelf();
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            return file == INVALID_HANDLE_VALUE && code == ERROR_ACCESS_DENIED;
        };
        check(denied_read(target), "restricted token cannot enumerate protected target before copy");
        write(target / L"probe.pulse-copy-temp");
        check(denied_read(target / L"probe.pulse-copy-temp"), "temporary output inherits private ACL from creation");
        fs::remove(target / L"probe.pulse-copy-temp");
        check(CopyIndexForMigration(source.wstring(), target.wstring(), migration, error, true), "production migration copies into private target");
        check(denied_read(target / L"pulse-index.bin"), "restricted token cannot read published index");
        IndexMigration resume;
        check(CopyIndexForMigration(source.wstring(), target.wstring(), resume, error, true), "private matching target resumes safely");
        DiscardIndexMigrationCopies(resume);
        check(fs::exists(target / L"pulse-index.bin"), "resume rollback preserves prior copy");
        DiscardIndexMigrationCopies(migration);
        check(fs::exists(source / L"pulse-index.bin") && !fs::exists(target / L"pulse-index.bin"), "failed activation rollback retains source");
    }
    if (restricted) CloseHandle(restricted);
    fs::remove_all(root);
    return failures ? 1 : 0;
}
