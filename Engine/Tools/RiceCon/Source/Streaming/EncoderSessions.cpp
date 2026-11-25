#include "EncoderSessions.h"
#include <stdexcept>

namespace RiceController::Streaming {
namespace {
bool SameEncoding(const OutputProfile& a, const OutputProfile& b) {
    return a.width == b.width && a.height == b.height && a.fps == b.fps &&
        a.codec == b.codec && a.encoder == b.encoder && a.videoBitrateKbps == b.videoBitrateKbps &&
        a.constantBitrate == b.constantBitrate && a.keyframeSeconds == b.keyframeSeconds;
}
}
EncoderSessions::EncoderSessions(Factory factory) : factory_(std::move(factory)) {
    if (!factory_) throw std::invalid_argument("Encoder factory is required");
}
std::shared_ptr<VideoEncoder> EncoderSessions::Acquire(const std::string& slotId,
        const std::string& sourceId, const OutputProfile& profile) {
    ValidateProfile(profile);
    if (slotId.empty() || sourceId.empty()) throw std::invalid_argument("Slot and source IDs are required");
    if (slots_.contains(slotId)) throw std::logic_error("Slot already owns an encoder");
    if (slots_.size() >= SlotRegistry::MaxConcurrentOutputs)
        throw std::runtime_error("Maximum concurrent output count reached");
    std::shared_ptr<VideoEncoder> encoder;
    for (const auto& [id, session] : slots_) {
        if (session.sourceId == sourceId && SameEncoding(session.profile, profile)) {
            encoder = session.encoder;
            break;
        }
    }
    if (!encoder) encoder = factory_(profile);
    if (!encoder) throw std::runtime_error("Hardware encoder initialization returned no session");
    slots_.emplace(slotId, Session{sourceId, profile, encoder});
    return encoder;
}
void EncoderSessions::Release(const std::string& slotId) noexcept { slots_.erase(slotId); }
}
