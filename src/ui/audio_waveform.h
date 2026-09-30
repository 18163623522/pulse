#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

namespace pulse::ui {

// Peak envelope of an audio file for the Quick Look audio preview.
// Decoding runs on a detached worker (Media Foundation source reader, loaded on
// demand so Windows N without Media Foundation simply gets no waveform). Each
// Start has its own mailbox; a retired worker can never publish into the next
// file's waveform, and Reset/Start only raise its stop flag, never block.
class AudioWaveform {
public:
    static constexpr size_t kBuckets = 600;
    AudioWaveform() = default;
    ~AudioWaveform();
    AudioWaveform(const AudioWaveform&) = delete;
    AudioWaveform& operator=(const AudioWaveform&) = delete;

    void Start(const std::wstring& path);
    void Reset();
    // Normalized peaks (0..1, kBuckets entries) decoded so far; buckets not yet
    // reached are zero. progress is 0..1; failed means "draw a plain track".
    bool Snapshot(std::vector<float>& peaks, float& progress, bool& failed) const;
    bool active() const noexcept { return state_ != nullptr; }

private:
    struct Shared;
    static void Run(std::shared_ptr<Shared> state, std::wstring path);
    std::shared_ptr<Shared> state_;
};

} // namespace pulse::ui
