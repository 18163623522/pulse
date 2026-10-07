#pragma once
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <filesystem>
#include <vector>
#include <algorithm>

namespace pulse::index {
// Pin every path component from the volume root down. Protecting only the
// leaf does not stop an attacker from renaming a writable parent directory.
class PrivateIndexDirectoryLock {
public:
    PrivateIndexDirectoryLock() = default;
    ~PrivateIndexDirectoryLock() { for (HANDLE handle : handles_) CloseHandle(handle); }
    PrivateIndexDirectoryLock(const PrivateIndexDirectoryLock&) = delete;
    PrivateIndexDirectoryLock& operator=(const PrivateIndexDirectoryLock&) = delete;
    bool Acquire(const std::filesystem::path& path) {
        if (!handles_.empty() || !path.is_absolute()) return false;
        std::vector<std::filesystem::path> parts;
        for (auto part = path; !part.empty(); part = part.parent_path()) {
            parts.push_back(part);
            if (part == part.parent_path()) break;
        }
        std::reverse(parts.begin(), parts.end());
        for (const auto& part : parts) {
            HANDLE handle = CreateFileW(part.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return false;
            handles_.push_back(handle);
            FILE_ATTRIBUTE_TAG_INFO tag{};
            if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) ||
                !(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        }
        return !handles_.empty();
    }
private:
    std::vector<HANDLE> handles_;
};

// A nonempty custom directory is accepted only if it already has the service
// policy. Never rewrite ACLs on unrelated user files to make a target usable.
inline bool IsPrivateIndexObject(const std::filesystem::path& path, bool require_protected = false) {
    HANDLE handle = CreateFileW(path.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    FILE_ATTRIBUTE_TAG_INFO tag{};
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    bool ok = GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) &&
        !(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            &owner, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS && acl &&
        (IsWellKnownSid(owner, WinBuiltinAdministratorsSid) || IsWellKnownSid(owner, WinLocalSystemSid));
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (ok && require_protected)
        ok = GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED);
    bool system = false, admin = false;
    for (DWORD i = 0; ok && i < acl->AceCount; ++i) {
        void* value = nullptr;
        if (!GetAce(acl, i, &value)) { ok = false; break; }
        auto ace = static_cast<ACCESS_ALLOWED_ACE*>(value);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
            (ace->Header.AceFlags & INHERIT_ONLY_ACE)) { ok = false; break; }
        PSID sid = &ace->SidStart;
        const bool sy = IsWellKnownSid(sid, WinLocalSystemSid) != FALSE;
        const bool ba = IsWellKnownSid(sid, WinBuiltinAdministratorsSid) != FALSE;
        ok = (sy || ba) && (ace->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS;
        if (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ok = ok && (ace->Header.AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) ==
                (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
        system |= sy; admin |= ba;
    }
    if (descriptor) LocalFree(descriptor);
    CloseHandle(handle);
    return ok && system && admin;
}

inline bool PreparePrivateIndexDirectory(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    try {
        if (!path.is_absolute() || path == path.root_path()) return false;
        for (auto part = path; !part.empty(); part = part.parent_path()) {
            const DWORD attributes = GetFileAttributesW(part.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
            if (part == part.parent_path()) break;
        }
        fs::create_directories(path);
        if (!IsPrivateIndexObject(path, true)) {
            if (!fs::is_empty(path)) return false;
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"O:BAG:SYD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", SDDL_REVISION_1, &descriptor, nullptr)) return false;
            const BOOL changed = SetFileSecurityW(path.c_str(), OWNER_SECURITY_INFORMATION |
                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
            LocalFree(descriptor);
            if (!changed || !IsPrivateIndexObject(path, true)) return false;
        }
        for (const auto& entry : fs::recursive_directory_iterator(path))
            if (!IsPrivateIndexObject(entry.path())) return false;
        return true;
    } catch (...) { return false; }
}
}

namespace pulse::index::security {
inline bool ServiceSid(PSID sid) {
    return sid && (IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinBuiltinAdministratorsSid));
}
inline bool PrivateDescriptor(PSECURITY_DESCRIPTOR descriptor, bool directory, bool protected_root) {
    PSID owner = nullptr;
    BOOL defaulted = FALSE, present = FALSE;
    PACL dacl = nullptr;
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!descriptor || !GetSecurityDescriptorOwner(descriptor, &owner, &defaulted) || !ServiceSid(owner) ||
        !GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) || !present || !dacl ||
        !GetSecurityDescriptorControl(descriptor, &control, &revision) ||
        (protected_root && !(control & SE_DACL_PROTECTED))) return false;
    bool system = false, administrators = false;
    for (DWORD i = 0; i < dacl->AceCount; ++i) {
        void* raw = nullptr;
        if (!GetAce(dacl, i, &raw)) return false;
        const auto* header = static_cast<ACE_HEADER*>(raw);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        // Unknown/callback/object grants cannot be conservatively proven private.
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) return false;
        const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        auto sid = const_cast<DWORD*>(&ace->SidStart);
        if (!ServiceSid(sid)) return false;
        if ((ace->Mask & FILE_ALL_ACCESS) != FILE_ALL_ACCESS || (header->AceFlags & INHERIT_ONLY_ACE)) continue;
        if (directory && (header->AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) !=
            (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) continue;
        system |= IsWellKnownSid(sid, WinLocalSystemSid) != FALSE;
        administrators |= IsWellKnownSid(sid, WinBuiltinAdministratorsSid) != FALSE;
    }
    return system && administrators;
}
inline bool PrivateObject(const std::wstring& path, bool protected_root = false) {
    HANDLE handle = CreateFileW(path.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    FILE_ATTRIBUTE_TAG_INFO tag{};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const bool ok = GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) &&
        !(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            nullptr, nullptr, nullptr, nullptr, &descriptor) == ERROR_SUCCESS &&
        PrivateDescriptor(descriptor, (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, protected_root);
    if (descriptor) LocalFree(descriptor);
    CloseHandle(handle);
    return ok;
}
inline bool PrivateTree(const std::wstring& path) {
    if (!PrivateObject(path, true)) return false;
    std::error_code error;
    std::filesystem::recursive_directory_iterator entry(path, error), end;
    if (error) return false;
    while (entry != end) {
        if (!PrivateObject(entry->path().wstring())) return false;
        entry.increment(error);
        if (error) return false;
    }
    return true;
}
class PrivateAttributes {
public:
    PrivateAttributes() {
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:SYD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", SDDL_REVISION_1, &descriptor_, nullptr))
            attributes_.lpSecurityDescriptor = descriptor_;
    }
    ~PrivateAttributes() { if (descriptor_) LocalFree(descriptor_); }
    PrivateAttributes(const PrivateAttributes&) = delete;
    PrivateAttributes& operator=(const PrivateAttributes&) = delete;
    explicit operator bool() const { return descriptor_ != nullptr; }
    SECURITY_ATTRIBUTES* get() { return descriptor_ ? &attributes_ : nullptr; }
private:
    SECURITY_ATTRIBUTES attributes_{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
};
} // namespace pulse::index::security
