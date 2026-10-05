// pack_installer_test - download, verification and staging of preview packs,
// with an in-memory "server" in place of WinHTTP.
#include "../app/pack_installer.h"
#include "../common/utf8_file.h"
#include <bcrypt.h>
#include <compressapi.h>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace pulse;
using namespace pulse::app;

namespace {
int g_failed = 0, g_passed = 0;
void Check(bool ok, const char* what) {
    if (ok) { ++g_passed; return; }
    ++g_failed;
    std::printf("FAIL: %s\n", what);
}

std::vector<uint8_t> Lzms(const std::vector<uint8_t>& in) {
    COMPRESSOR_HANDLE h = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &h)) return {};
    SIZE_T needed = 0;
    Compress(h, in.data(), in.size(), nullptr, 0, &needed);
    std::vector<uint8_t> out(needed);
    SIZE_T wrote = 0;
    if (!Compress(h, in.data(), in.size(), out.data(), out.size(), &wrote)) out.clear();
    else out.resize(wrote);
    CloseCompressor(h);
    return out;
}

std::wstring Sha256Hex(const std::vector<uint8_t>& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    uint8_t digest[32]{};
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    BCryptHashData(hash, const_cast<uint8_t*>(data.data()), static_cast<ULONG>(data.size()), 0);
    BCryptFinishHash(hash, digest, 32, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    std::wstring hex;
    for (uint8_t b : digest) { wchar_t t[3]; swprintf(t, 3, L"%02x", b); hex += t; }
    return hex;
}

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::vector<uint8_t> ReadAll(const std::wstring& p) {
    std::vector<uint8_t> out;
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"rb") || !f) return out;
    uint8_t buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return out;
}

// A pack of two files plus the bytes the fake server hands out.
struct Fixture {
    std::vector<std::vector<uint8_t>> plain, packed;
    std::vector<std::wstring> names, sha, packed_sha;
    std::vector<PackFile> files;
    std::map<std::wstring, std::vector<uint8_t>> server;
    PackRelease release{};
    std::wstring version;

    explicit Fixture(const wchar_t* ver) : version(ver) {
        std::vector<uint8_t> big(3u << 20);
        uint32_t x = 12345;
        for (size_t i = 0; i < big.size(); ++i) {   // half noise, half runs
            x = x * 1664525u + 1013904223u;
            big[i] = (i / 4096) % 2 ? static_cast<uint8_t>(x >> 24) : static_cast<uint8_t>(i / 4096);
        }
        const std::string note = "Pulse FFmpeg preview pack\r\nLicense: LGPL-2.1\r\n";
        plain = {big, std::vector<uint8_t>(note.begin(), note.end())};
        names = {L"ffmpeg.exe", L"SOURCE.txt"};
        for (size_t i = 0; i < plain.size(); ++i) {
            packed.push_back(Lzms(plain[i]));
            sha.push_back(Sha256Hex(plain[i]));
            packed_sha.push_back(Sha256Hex(packed[i]));
        }
        for (size_t i = 0; i < plain.size(); ++i) {
            files.push_back({names[i].c_str(), plain[i].size(), sha[i].c_str(),
                             packed[i].size(), packed_sha[i].c_str()});
            server[L"https://packs.example.invalid/v/" + names[i] + L".lzms"] = packed[i];
        }
        release = {L"ffmpeg", version.c_str(), L"https://packs.example.invalid/v/", files.data(), files.size()};
    }

    UpdateResponseReader Reader(size_t chunk = 7000, std::atomic<bool>* cancel_after_first = nullptr) {
        return [this, chunk, cancel_after_first](std::wstring_view url, uint64_t maximum,
                   const std::atomic<bool>& cancelled, const std::function<bool(const void*, DWORD)>& consume,
                   UpdateError& category, DWORD& error) {
            auto it = server.find(std::wstring(url));
            if (it == server.end()) { category = UpdateError::HttpStatus; error = 404; return false; }
            const auto& body = it->second;
            if (body.size() > maximum) { category = UpdateError::ResponseTooLarge; error = ERROR_FILE_TOO_LARGE; return false; }
            for (size_t at = 0; at < body.size(); at += chunk) {
                if (cancelled.load()) { error = ERROR_CANCELLED; return false; }
                const DWORD n = static_cast<DWORD>((std::min)(chunk, body.size() - at));
                if (!consume(body.data() + at, n)) { category = UpdateError::LocalIo; error = ERROR_WRITE_FAULT; return false; }
                if (cancel_after_first) cancel_after_first->store(true);
            }
            return true;
        };
    }
};

std::wstring MakeRoot() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring root = std::wstring(tmp) + L"pulse_pack_installer_test_" + std::to_wstring(GetCurrentProcessId());
    RemovePackTree(root);
    CreateDirectoryW(root.c_str(), nullptr);
    return root;
}
} // namespace

int main() {
    const std::wstring root = MakeRoot();
    const std::wstring base = root + L"\\ffmpeg";

    {   // Happy path, with an older version and a stale staging folder around.
        Fixture fx(L"7.1.1");
        Check(!fx.packed[0].empty() && fx.packed[0].size() < fx.plain[0].size(), "LZMS shrinks the fixture");
        CreateDirectoryW(base.c_str(), nullptr);
        CreateDirectoryW((base + L"\\7.0").c_str(), nullptr);
        WriteUtf8FileAtomic(base + L"\\7.0\\ffmpeg.exe", L"old");
        CreateDirectoryW((base + L"\\7.1.1.partial").c_str(), nullptr);
        WriteUtf8FileAtomic(base + L"\\7.1.1.partial\\junk", L"junk");
        PackInstallProgress progress;
        DWORD error = 0;
        const auto outcome = InstallPack(fx.release, root, progress, error, fx.Reader());
        Check(outcome == PackInstallOutcome::Installed, "pack installs");
        Check(error == ERROR_SUCCESS, "no error on success");
        Check(ReadAll(base + L"\\7.1.1\\ffmpeg.exe") == fx.plain[0], "big file decompressed byte-exact");
        Check(ReadAll(base + L"\\7.1.1\\SOURCE.txt") == fx.plain[1], "small file decompressed byte-exact");
        Check(!Exists(base + L"\\7.1.1\\ffmpeg.exe.lzms"), "compressed download removed");
        Check(!Exists(base + L"\\7.1.1\\junk"), "stale staging content not carried over");
        Check(!Exists(base + L"\\7.1.1.partial"), "staging folder renamed away");
        Check(!Exists(base + L"\\7.0"), "older version removed");
        std::wstring json;
        Check(ReadUtf8File(base + L"\\installed.json", json) &&
              json.find(L"\"version\":\"7.1.1\"") != std::wstring::npos &&
              json.find(L"\"dir\":\"7.1.1\"") != std::wstring::npos, "installed.json names the version");
        Check(progress.total.load() == fx.packed[0].size() + fx.packed[1].size() &&
              progress.received.load() == progress.total.load(), "progress reaches the total");

        // Reinstalling the same version replaces it in place.
        PackInstallProgress again;
        Check(InstallPack(fx.release, root, again, error, fx.Reader(65536)) == PackInstallOutcome::Installed,
              "same version reinstalls");
        Check(ReadAll(base + L"\\7.1.1\\ffmpeg.exe") == fx.plain[0], "reinstalled file intact");
    }

    {   // A tampered download is discarded and the installed pack stays.
        Fixture fx(L"7.2");
        auto& body = fx.server[L"https://packs.example.invalid/v/ffmpeg.exe.lzms"];
        Check(body.size() > 1000, "tamper target is the big file");
        body[body.size() / 2] ^= 0x5a;
        PackInstallProgress progress;
        DWORD error = 0;
        Check(InstallPack(fx.release, root, progress, error, fx.Reader()) == PackInstallOutcome::Failed,
              "tampered download fails");
        Check(error == ERROR_INVALID_DATA, "tampering reports invalid data");
        Check(!Exists(base + L"\\7.2") && !Exists(base + L"\\7.2.partial"), "nothing of the failed version stays");
        std::wstring json;
        Check(ReadUtf8File(base + L"\\installed.json", json) && json.find(L"7.1.1") != std::wstring::npos,
              "previous installed.json untouched");
        Check(Exists(base + L"\\7.1.1\\ffmpeg.exe"), "previous pack untouched");
    }

    {   // The decompressed file must match its own hash too.
        Fixture fx(L"7.3");
        fx.sha[1][0] = fx.sha[1][0] == L'0' ? L'1' : L'0';
        fx.files[1].sha256 = fx.sha[1].c_str();
        PackInstallProgress progress;
        DWORD error = 0;
        Check(InstallPack(fx.release, root, progress, error, fx.Reader()) == PackInstallOutcome::Failed &&
              error == ERROR_INVALID_DATA, "plain hash mismatch fails");
    }

    {   // A response longer than pinned is cut off.
        Fixture fx(L"7.4");
        fx.files[1].packed_size -= 1;
        PackInstallProgress progress;
        DWORD error = 0;
        Check(InstallPack(fx.release, root, progress, error, fx.Reader()) == PackInstallOutcome::Failed,
              "oversized response fails");
        Check(!Exists(base + L"\\7.4.partial"), "oversized response leaves no staging");
    }

    {   // Missing file on the server.
        Fixture fx(L"7.5");
        fx.server.clear();
        PackInstallProgress progress;
        DWORD error = 0;
        Check(InstallPack(fx.release, root, progress, error, fx.Reader()) == PackInstallOutcome::Failed,
              "404 fails");
    }

    {   // Cancelling mid-download.
        Fixture fx(L"7.6");
        PackInstallProgress progress;
        DWORD error = 0;
        Check(InstallPack(fx.release, root, progress, error, fx.Reader(7000, &progress.cancelled)) ==
              PackInstallOutcome::Cancelled, "cancel reports cancelled");
        Check(error == ERROR_CANCELLED, "cancel error code");
        Check(!Exists(base + L"\\7.6") && !Exists(base + L"\\7.6.partial"), "cancel leaves nothing");
    }

    {   // Catalog values never become paths outside the pack folder.
        Fixture fx(L"..");
        PackInstallProgress progress;
        DWORD error = 0;
        Check(InstallPack(fx.release, root, progress, error, fx.Reader()) == PackInstallOutcome::Failed &&
              error == ERROR_INVALID_PARAMETER, "'..' version rejected");
        Fixture bad_name(L"8.0");
        bad_name.files[0].name = L"..\\evil.exe";
        Check(InstallPack(bad_name.release, root, progress, error, bad_name.Reader()) == PackInstallOutcome::Failed &&
              error == ERROR_INVALID_PARAMETER, "path in file name rejected");
        Fixture http(L"8.1");
        http.release.base_url = L"http://packs.example.invalid/v/";
        Check(InstallPack(http.release, root, progress, error, http.Reader()) == PackInstallOutcome::Failed &&
              error == ERROR_INVALID_PARAMETER, "plain http rejected");
        PackRelease empty{L"ffmpeg", L"9.0", L"https://x.invalid/", nullptr, 0};
        Check(InstallPack(empty, root, progress, error) == PackInstallOutcome::Failed, "unpublished pack rejected");
        Check(!Exists(base + L"\\8.0") && !Exists(base + L"\\8.1"), "rejected releases create nothing");
    }

    {   // Async wrapper: one outcome, taken once.
        PackRelease empty{L"ffmpeg", L"9.0", L"https://x.invalid/", nullptr, 0};
        PackInstaller installer;
        Check(installer.Start(empty, nullptr), "installer starts");
        for (int i = 0; i < 200 && installer.running(); ++i) Sleep(10);
        Check(!installer.running(), "installer finishes");
        DWORD error = 0;
        Check(installer.TakeOutcome(error) == PackInstallOutcome::Failed, "outcome reported");
        Check(installer.TakeOutcome(error) == PackInstallOutcome::None, "outcome reported once");
    }

    RemovePackTree(root);
    std::printf("pack_installer_test: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
