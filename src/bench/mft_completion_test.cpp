#include "../index/index_mft.h"
#include <vector>
#include <cstring>
#include <cstdio>

int main() {
    using namespace pulse::index;
    int failures = 0;
    auto check = [&](bool ok, const char* label) { printf("[%s] %s\n", ok ? "PASS" : "FAIL", label); if (!ok) ++failures; };
    NTFS_VOLUME_DATA_BUFFER geometry{};
    geometry.BytesPerFileRecordSegment = 512; geometry.BytesPerCluster = 512; geometry.BytesPerSector = 512;
    geometry.MftValidDataLength.QuadPart = 8 * 1024 * 1024 + 512;
    auto record = [] (BYTE* data) {
        auto u16 = [&](size_t at, uint16_t value) { memcpy(data + at, &value, sizeof(value)); };
        auto u32 = [&](size_t at, uint32_t value) { memcpy(data + at, &value, sizeof(value)); };
        u32(0, 0x454c4946); u16(4, 42); u16(6, 1); u16(16, 1);
        u16(20, 48); u16(22, 1); u32(24, 160); u32(28, 512);
        u32(48, 0x30); u32(52, 96); u32(64, 68); u16(68, 24);
        data[72 + 64] = 1; data[72 + 65] = 1; u16(72 + 66, L'x'); u32(144, 0xffffffff);
    };
    for (int mode = 0; mode < 4; ++mode) {
        unsigned reads = 0, emitted = 0;
        std::atomic<bool> running{true};
        const bool complete = EnumerateMftRecords(geometry, [&](uint64_t, void* bytes, DWORD size) {
            ++reads;
            if (mode == 1 && reads == 3) return false;
            memset(bytes, 0, size);
            if (reads == 2) record(static_cast<BYTE*>(bytes));
            return true;
        }, &running, {}, [&](MftFile&& file) {
            ++emitted;
            if (file.name != L"x") return false;
            if (mode == 2) running = false;
            return mode != 3;
        });
        check(emitted == 1 && complete == (mode == 0), mode == 0 ? "complete source succeeds after all chunks" :
            mode == 1 ? "read failure after emitted records stays incomplete" :
            mode == 2 ? "cancellation after first record stays incomplete" : "consumer stop never reports complete volume");
    }
    return failures ? 1 : 0;
}
