// Keep MF header feature gates consistent with the Windows 8.1 app target
// (same as video_preview.cpp).
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x06030000
#include "audio_waveform.h"
#include "../common/path_utils.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

#pragma comment(lib, "mfuuid.lib")

namespace pulse::ui {
using Microsoft::WRL::ComPtr;

namespace {
// Longer recordings (podcasts, audiobooks) would keep a core busy for many
// seconds; they get the plain progress track instead.
constexpr int64_t kMaxDuration = 30ll * 60 * 10000000;  // 30 min in 100 ns
}  // namespace

struct AudioWaveform::Shared {
    mutable std::mutex mutex;
    std::vector<float> peaks = std::vector<float>(kBuckets, 0.0f);
    float progress = 0.0f;
    bool failed = false;
    std::atomic<bool> stop{false};
};

AudioWaveform::~AudioWaveform() { Reset(); }

void AudioWaveform::Start(const std::wstring& path) {
    Reset();
    state_ = std::make_shared<Shared>();
    try {
        std::thread(Run, state_, path).detach();
    } catch (...) {
        state_->failed = true;
    }
}

void AudioWaveform::Reset() {
    if (state_) state_->stop.store(true);
    state_.reset();
}

bool AudioWaveform::Snapshot(std::vector<float>& peaks, float& progress, bool& failed) const {
    if (!state_) return false;
    std::lock_guard lock(state_->mutex);
    peaks = state_->peaks;
    progress = state_->progress;
    failed = state_->failed;
    return true;
}

void AudioWaveform::Run(std::shared_ptr<Shared> state, std::wstring path) {
    auto fail = [&] {
        std::lock_guard lock(state->mutex);
        state->failed = true;
    };
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) { fail(); return; }
    struct ComScope { ~ComScope() { CoUninitialize(); } } com_scope;

    HMODULE plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE readwrite = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    struct Libraries {
        HMODULE a, b;
        ~Libraries() { if (b) FreeLibrary(b); if (a) FreeLibrary(a); }
    } libraries{plat, readwrite};
    if (!plat || !readwrite) { fail(); return; }
    using Startup = HRESULT (WINAPI*)(ULONG, DWORD);
    using Shutdown = HRESULT (WINAPI*)();
    using CreateType = HRESULT (WINAPI*)(IMFMediaType**);
    using CreateReader = HRESULT (WINAPI*)(LPCWSTR, IMFAttributes*, IMFSourceReader**);
    const auto startup = reinterpret_cast<Startup>(GetProcAddress(plat, "MFStartup"));
    const auto shutdown = reinterpret_cast<Shutdown>(GetProcAddress(plat, "MFShutdown"));
    const auto create_type = reinterpret_cast<CreateType>(GetProcAddress(plat, "MFCreateMediaType"));
    const auto create_reader = reinterpret_cast<CreateReader>(
        GetProcAddress(readwrite, "MFCreateSourceReaderFromURL"));
    if (!startup || !shutdown || !create_type || !create_reader ||
        FAILED(startup(MF_VERSION, MFSTARTUP_LITE))) { fail(); return; }
    struct MfScope { Shutdown s; ~MfScope() { s(); } } mf_scope{shutdown};

    const std::wstring url = pulse::path::StripExtendedPathPrefix(path);
    ComPtr<IMFSourceReader> reader;
    if (FAILED(create_reader(url.c_str(), nullptr, &reader))) { fail(); return; }
    const DWORD audio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader->SetStreamSelection(audio, TRUE))) { fail(); return; }
    ComPtr<IMFMediaType> wanted;
    if (FAILED(create_type(&wanted)) ||
        FAILED(wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float)) ||
        FAILED(reader->SetCurrentMediaType(audio, nullptr, wanted.Get()))) { fail(); return; }
    ComPtr<IMFMediaType> actual;
    if (FAILED(reader->GetCurrentMediaType(audio, &actual))) { fail(); return; }
    const UINT32 channels = MFGetAttributeUINT32(actual.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
    const UINT32 rate = MFGetAttributeUINT32(actual.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    if (!channels || !rate) { fail(); return; }

    PROPVARIANT value{};
    PropVariantInit(&value);
    int64_t duration = 0;
    if (SUCCEEDED(reader->GetPresentationAttribute(
            static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value)) &&
        value.vt == VT_UI8)
        duration = static_cast<int64_t>(value.uhVal.QuadPart);
    PropVariantClear(&value);
    if (duration <= 0 || duration > kMaxDuration) { fail(); return; }

    std::vector<float> raw(kBuckets, 0.0f);
    float loudest = 0.0f;
    auto publish = [&](float progress) {
        const float scale = 1.0f / (std::max)(loudest, 0.05f);
        std::lock_guard lock(state->mutex);
        for (size_t i = 0; i < kBuckets; ++i) state->peaks[i] = (std::min)(1.0f, raw[i] * scale);
        state->progress = progress;
    };
    auto last_publish = std::chrono::steady_clock::now();
    const double buckets_per_tick = static_cast<double>(kBuckets) / static_cast<double>(duration);
    const double ticks_per_frame = 1e7 / static_cast<double>(rate);
    while (!state->stop.load()) {
        DWORD index = 0, flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(audio, 0, &index, &flags, &timestamp, &sample))) break;
        if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) break;
        if (!sample) continue;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
        BYTE* data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length))) continue;
        const auto* samples = reinterpret_cast<const float*>(data);
        const size_t frames = length / sizeof(float) / channels;
        for (size_t f = 0; f < frames; ++f) {
            float peak = 0.0f;
            for (UINT32 c = 0; c < channels; ++c)
                peak = (std::max)(peak, std::fabs(samples[f * channels + c]));
            if (!std::isfinite(peak)) continue;
            const double at = static_cast<double>(timestamp) + static_cast<double>(f) * ticks_per_frame;
            const size_t bucket = static_cast<size_t>((std::clamp)(at * buckets_per_tick, 0.0,
                static_cast<double>(kBuckets - 1)));
            raw[bucket] = (std::max)(raw[bucket], peak);
            loudest = (std::max)(loudest, peak);
        }
        buffer->Unlock();
        const auto now = std::chrono::steady_clock::now();
        if (now - last_publish > std::chrono::milliseconds(200)) {
            last_publish = now;
            publish(static_cast<float>((std::clamp)(static_cast<double>(timestamp) /
                                                    static_cast<double>(duration), 0.0, 1.0)));
        }
    }
    if (!state->stop.load()) publish(1.0f);
}

} // namespace pulse::ui
