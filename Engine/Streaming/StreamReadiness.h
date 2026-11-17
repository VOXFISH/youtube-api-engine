#pragma once
#include <cstdint>
#include <mutex>

namespace Rice::Streaming {
// Each Player has one game instance. Tokens also protect editor play/restart
// from late asynchronous callbacks belonging to an earlier preparation.
class StreamReadiness {
    inline static std::mutex mutex_;
    inline static std::uint64_t generation_=0;
    inline static bool ready_=false,retired_=true;
public:
    static std::uint64_t BeginInstance() {
        std::lock_guard lock(mutex_);++generation_;ready_=false;retired_=false;return generation_;
    }
    static std::uint64_t Token() { std::lock_guard lock(mutex_);return generation_; }
    static bool Signal(std::uint64_t token) {
        std::lock_guard lock(mutex_);
        if(retired_ || token!=generation_)return false;
        ready_=true;return true;
    }
    static bool IsReady() { std::lock_guard lock(mutex_);return ready_ && !retired_; }
    static void Retire() { std::lock_guard lock(mutex_);retired_=true;ready_=false; }
};
// Call only after the game's own initialization/loading has succeeded.
// Async callbacks should capture Token() before starting their work.
inline bool ReadyForStream(std::uint64_t token) { return StreamReadiness::Signal(token); }
inline bool ReadyForStream() { return ReadyForStream(StreamReadiness::Token()); }
}
