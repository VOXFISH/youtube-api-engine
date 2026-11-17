#pragma once
#include "VideoEncoder.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <span>

namespace RiceController::Streaming {
// Each destination owns a muxer and network connection. Call on that output's
// worker, never on the game, preview, or another destination's worker.
class EncodedOutput {
public:
    virtual ~EncodedOutput() = default;
    virtual void WriteVideo(const EncodedVideoPacket& packet) = 0;
    // Exactly AudioFrameSamples() interleaved stereo frames per call; samplePts
    // is measured in 1/audioSampleRate on the same origin as video PTS.
    virtual int AudioFrameSamples() const noexcept = 0;
    virtual void WriteAudio(std::span<const float> stereo, std::int64_t samplePts) = 0;
    virtual void Finish() = 0;
};
// Local FLV output is for deterministic testing. Network output only permits
// RTMPS, verifies TLS, and has cancellation plus a per-operation deadline.
std::unique_ptr<EncodedOutput> OpenLocalOutput(const std::filesystem::path& path,
    const EncodedVideoFormat& video);
std::unique_ptr<EncodedOutput> OpenRtmpsOutput(const std::string& url,
    const EncodedVideoFormat& video, std::shared_ptr<std::atomic_bool> cancelled);
}
