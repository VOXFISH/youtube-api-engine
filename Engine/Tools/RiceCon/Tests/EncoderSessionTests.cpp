#include "Streaming/EncoderSessions.h"
#include <iostream>
#include <stdexcept>
using namespace RiceController::Streaming;
namespace {
void Check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
class FakeEncoder final : public VideoEncoder {
public:
    EncodedVideoFormat Format() const override { return {}; }
    std::vector<EncodedVideoPacket> EncodeRgba(std::span<const std::uint8_t>, int, std::int64_t) override { return {}; }
    std::vector<EncodedVideoPacket> Drain() override { return {}; }
};
}
int main() {
    try {
        int attempts = 0;
        bool fail = false;
        EncoderSessions sessions([&](const OutputProfile&) -> std::unique_ptr<VideoEncoder> {
            ++attempts;
            if (fail) throw std::runtime_error("Hardware session limit");
            return std::make_unique<FakeEncoder>();
        });
        OutputProfile profile;
        auto software = profile; software.encoder = "libx264";
        bool softwareRejected = false;
        try { OpenHardwareVideoEncoder(software); } catch (const std::runtime_error&) { softwareRejected = true; }
        Check(softwareRejected, "CPU encoder selection rejected before hardware admission");
        auto first = sessions.Acquire("one", "game", profile);
        fail = true;
        bool rejected = false;
        try { sessions.Acquire("two", "other-game", profile); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected && sessions.SlotCount() == 1 && attempts == 2, "Failure rejects only new slot, without fallback attempts");
        auto shared = sessions.Acquire("three", "game", profile);
        Check(shared == first && attempts == 2, "Existing encoder remains usable; identical output shares session");
        auto changed = profile; changed.fps = 30;
        rejected = false;
        try { sessions.Acquire("four", "game", changed); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected && sessions.SlotCount() == 2 && attempts == 3, "Different encoding requires its own session");
        sessions.Release("one");
        Check(shared.use_count() >= 2 && sessions.SlotCount() == 1, "Stopping one output preserves shared session");
        sessions.Acquire("a", "game", profile); sessions.Acquire("b", "game", profile);
        sessions.Acquire("c", "game", profile); sessions.Acquire("d", "game", profile);
        rejected = false;
        try { sessions.Acquire("six", "game", profile); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected && sessions.SlotCount() == 5 && attempts == 3, "Sixth output rejected before touching encoders");
        std::cout << "Encoder admission tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
