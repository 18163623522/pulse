#pragma once
// Preview pack releases this build of Pulse installs. Every file is pinned by
// size and SHA-256 here, so a download needs no signature or manifest: if the
// bytes do not match, they are thrown away. The CI job that publishes a pack
// prints the matching block (pack-catalog.inc); paste it below.
#include <cstddef>
#include <cstdint>

namespace pulse::app {

struct PackFile {
    const wchar_t* name;           // file name inside the pack directory
    uint64_t size;                 // after decompression
    const wchar_t* sha256;         // of the decompressed file
    uint64_t packed_size;          // <name>.lzms on the release
    const wchar_t* packed_sha256;  // of <name>.lzms
};

struct PackRelease {
    const wchar_t* key;            // packs::PackKey, the directory under packs\ .
    const wchar_t* version;        // also the version directory name
    const wchar_t* base_url;       // release download directory, ending in '/'
    const PackFile* files;
    size_t file_count;             // 0 while the pack is not published
};

// FFmpeg preview pack (decode-only LGPL build, packs/ffmpeg/build.sh).
inline constexpr PackRelease kMediaPackRelease{L"ffmpeg", L"", L"", nullptr, 0};

} // namespace pulse::app
