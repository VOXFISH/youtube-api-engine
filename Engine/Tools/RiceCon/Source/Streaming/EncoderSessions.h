#pragma once
#include "VideoEncoder.h"
#include <functional>
#include <map>
#include <string>

namespace RiceController::Streaming {
// Media worker only. Admission is transactional: construct the new session
// before adding its slot. No operation on failure touches existing sessions.
// Slots sharing a source and encoding settings share one hardware session.
class EncoderSessions {
public:
    using Factory = std::function<std::unique_ptr<VideoEncoder>(const OutputProfile&)>;
    explicit EncoderSessions(Factory factory = OpenHardwareVideoEncoder);
    std::shared_ptr<VideoEncoder> Acquire(const std::string& slotId,
        const std::string& sourceId, const OutputProfile& profile);
    void Release(const std::string& slotId) noexcept;
    std::size_t SlotCount() const noexcept { return slots_.size(); }
private:
    struct Session {
        std::string sourceId;
        OutputProfile profile;
        std::shared_ptr<VideoEncoder> encoder;
    };
    Factory factory_;
    std::map<std::string, Session> slots_;
};
}
