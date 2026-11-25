#pragma once
#include "SlotModel.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace RiceController::Streaming {
struct EncodedVideoPacket {
    std::vector<std::uint8_t> bytes;
    std::int64_t pts = 0, dts = 0, duration = 0;
    bool keyframe = false;
};
struct EncodedVideoFormat {
    OutputProfile profile;
    std::vector<std::uint8_t> extraData;
};
// Worker-thread confined. Timestamp units are 1/profile.fps, including gaps in
// captured frames. Packet ownership is independent of the encoder for fan-out.
class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;
    virtual EncodedVideoFormat Format() const = 0;
    virtual std::vector<EncodedVideoPacket> EncodeRgba(
        std::span<const std::uint8_t> pixels, int stride, std::int64_t pts) = 0;
    virtual std::vector<EncodedVideoPacket> Drain() = 0;
};
// Opens the real hardware session before returning. Never falls back to a CPU
// codec, lower resolution, lower frame rate, or a different codec.
std::unique_ptr<VideoEncoder> OpenHardwareVideoEncoder(const OutputProfile& profile);
}
