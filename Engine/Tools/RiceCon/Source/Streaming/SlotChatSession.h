#pragma once
#include "YouTubeClient.h"
#include <future>
#include <stop_token>
namespace RiceController::Streaming {
// Owns the serial polling worker for one slot. The sink must enqueue work onto
// the Controller/game owner thread; it must not execute game functions here.
class SlotChatSession {
public:
    using Sink=std::function<void(ChatMessage,std::uint64_t)>;
    SlotChatSession(std::unique_ptr<YouTubeClient> client,std::string liveChatId,Sink sink);
    ~SlotChatSession();
    std::future<void> Ready(std::stop_token cancelled);
    void SetMuted(bool muted);
    std::string Error() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
