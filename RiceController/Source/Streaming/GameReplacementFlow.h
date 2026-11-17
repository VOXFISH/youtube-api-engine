#pragma once
#include "ReplacementLifecycle.h"
#include "WaitingImage.h"
#include "../ProcessManager.h"
#include <optional>
#include <stop_token>

namespace RiceController::Streaming {
// Actual destination adapters must acknowledge after their worker applies the
// source at a frame boundary. No default success/no-op adapters are provided.
struct ReplacementServices {
    // Adapters must observe cancellation before applying a source/data/chat
    // handoff, and serialize media changes per slot. A late cancelled operation
    // must never overwrite the subsequently restored source.
    std::function<std::future<void>(const std::string&,std::shared_ptr<const RgbaImage>,std::stop_token)> showWaiting;
    std::function<std::future<void>(const std::string&,const std::string&,std::stop_token)> switchSource;
    std::function<std::future<void>(const std::string&,const std::string&,std::stop_token)> restoreData;
    std::function<std::future<void>(const std::string&,const std::string&,std::stop_token)> connectChat;
    // Synchronous routing gate; includes queued/in-flight command invalidation.
    // The adapter advances its epoch/cutoff on unmute so discarded input cannot replay.
    std::function<void(const std::string&,bool)> muteCommands;
};
class GameReplacementFlow {
public:
    enum class Phase { Idle, WaitingImage, Preparing, Switching, Restoring, Complete, Failed };
    GameReplacementFlow(SlotRegistry& slots,ProcessManager& processes,ReplacementServices services);
    void Begin(const std::string& slotId,const Entry& next,std::string instanceType);
    void Cancel();
    void Poll();
    Phase State() const noexcept { return phase_; }
    const std::string& Error() const noexcept { return error_; }
    bool Busy() const noexcept;
    ReplacementLifecycle& Retirement() noexcept { return retirement_; }
private:
    SlotRegistry& slots_;ProcessManager& processes_;ReplacementServices services_;
    ReplacementLifecycle retirement_;
    Phase phase_=Phase::Idle;
    Slot previous_;Entry next_;std::string nextType_,nextRuntime_,error_;
    bool started_=false,requestedPreparation_=false,cancel_=false;
    bool dataReady_=false,chatReady_=false;
    bool restored_=false;
    std::future<void> media_,data_,chat_;
    std::stop_source cancellation_;
    std::vector<std::future<void>> superseded_;
    std::shared_ptr<const RgbaImage> waiting_;
    void Restore(std::string error);
    static bool Completed(std::future<void>& task);
};
}
