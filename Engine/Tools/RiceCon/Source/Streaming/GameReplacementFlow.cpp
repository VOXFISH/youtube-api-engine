#include "GameReplacementFlow.h"
#include <chrono>
#include <stdexcept>
namespace RiceController::Streaming {
GameReplacementFlow::GameReplacementFlow(SlotRegistry& slots,ProcessManager& processes,ReplacementServices services)
    :slots_(slots),processes_(processes),services_(std::move(services)),retirement_(processes) {
    if(!services_.showWaiting||!services_.switchSource||!services_.restoreData||!services_.connectChat||!services_.muteCommands)
        throw std::invalid_argument("Replacement requires actual media, data and chat adapters");
}
bool GameReplacementFlow::Busy() const noexcept {
    return phase_==Phase::WaitingImage||phase_==Phase::Preparing||phase_==Phase::Switching||phase_==Phase::Restoring;
}
bool GameReplacementFlow::Completed(std::future<void>& task) {
    if(!task.valid())throw std::runtime_error("Replacement operation returned no acknowledgement");
    const auto status=task.wait_for(std::chrono::seconds(0));
    if(status==std::future_status::deferred)throw std::runtime_error("Replacement operation cannot execute on the Controller thread");
    if(status!=std::future_status::ready)return false;
    task.get();return true;
}
void GameReplacementFlow::Begin(const std::string& id,const Entry& next,std::string type) {
    if(Busy())throw std::runtime_error("A replacement is already in progress");
    if((data_.valid()&&data_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)||
       (chat_.valid()&&chat_.wait_for(std::chrono::seconds(0))!=std::future_status::ready))
        throw std::runtime_error("Previous preparation callbacks have not finished");
    previous_=slots_.Get(id);
    if(!IsActive(previous_.state))throw std::runtime_error("Replacement requires a confirmed active output");
    const auto old=processes_.Get(previous_.entryId);
    if(!old.alive || old.runId!=previous_.runtimeInstanceId || !old.canRetire)
        throw std::runtime_error("Current Player must support durable retirement");
    if(next.id==previous_.entryId)throw std::runtime_error("This entry already supplies the slot's shared instance");
    if(!SupportsControl(next.executable))throw std::runtime_error("New Player must support Controller commands");
    // Decode before changing the output; image failure preserves the live game.
    waiting_=std::make_shared<RgbaImage>(LoadWaitingImageUtf8(previous_.waitingImagePath,previous_.profile.width,previous_.profile.height));
    next_=next;nextType_=std::move(type);nextRuntime_.clear();error_.clear();
    started_=requestedPreparation_=cancel_=dataReady_=chatReady_=false;
    const auto target=processes_.Get(next.id);
    if(target.alive && target.retiring)throw std::runtime_error("Cannot reuse an instance that is retiring");
    slots_.ValidateAssignment(id,next.id,nextType_,target.alive?target.runId:NewId());
    cancellation_=std::stop_source{};
    services_.muteCommands(id,true);
    try {media_=services_.showWaiting(id,waiting_,cancellation_.get_token());phase_=Phase::WaitingImage;}
    catch(...){services_.muteCommands(id,false);throw;}
}
void GameReplacementFlow::Cancel(){if(Busy()){cancel_=true;cancellation_.request_stop();}}
void GameReplacementFlow::Restore(std::string error){
    error_=std::move(error);
    cancellation_.request_stop();
    if(media_.valid())superseded_.push_back(std::move(media_));
    media_=services_.switchSource(previous_.slotId,previous_.runtimeInstanceId,{});
    phase_=Phase::Restoring;restored_=false;
}
void GameReplacementFlow::Poll(){
    retirement_.Poll(slots_);
    for(auto it=superseded_.begin();it!=superseded_.end();){
        if(it->wait_for(std::chrono::seconds(0))==std::future_status::ready){try{it->get();}catch(...){}it=superseded_.erase(it);}
        else ++it;
    }
    try {
        if(cancel_ && (phase_==Phase::WaitingImage||phase_==Phase::Preparing||phase_==Phase::Switching)){
            Restore("Replacement cancelled");return;
        }
        if(phase_==Phase::WaitingImage){
            if(!Completed(media_))return;
            if(cancel_){Restore("Replacement cancelled");return;}
            auto target=processes_.Get(next_.id);
            if(!target.alive){
                auto main=previous_.slotId;
                bool hasSlots=false;
                for(const auto& slot:slots_.Slots())if(slot.entryId==next_.id){hasSlots=true;if(slot.isMain)main=slot.slotId;}
                processes_.Start(next_,"hidden",false,main);started_=true;target=processes_.Get(next_.id);
                if(hasSlots)slots_.BindEntry(next_.id,target.runId);
            }
            nextRuntime_=target.runId;
            slots_.ValidateAssignment(previous_.slotId,next_.id,nextType_,nextRuntime_);
            phase_=Phase::Preparing;
        }
        if(phase_==Phase::Preparing){
            if(cancel_){Restore("Replacement cancelled");return;}
            const auto target=processes_.Get(next_.id);
            if(!target.alive || target.runId!=nextRuntime_ || target.state==RunState::Error || target.retiring)
                throw std::runtime_error("Replacement Player failed or changed identity");
            if(!target.controlled)return;
            if(!target.canRetire)throw std::runtime_error("Replacement Player does not support durable retirement");
            if(!requestedPreparation_){
                const auto main=slots_.MainSlotFor(nextRuntime_);
                data_=services_.restoreData(main.empty()?previous_.slotId:main,nextRuntime_,cancellation_.get_token());
                chat_=services_.connectChat(previous_.slotId,nextRuntime_,cancellation_.get_token());requestedPreparation_=true;
            }
            if(!dataReady_)dataReady_=Completed(data_);
            if(!chatReady_)chatReady_=Completed(chat_);
            // Deliberately no elapsed-time fallback: scripts must signal ready.
            if(!dataReady_||!chatReady_||!target.scriptReady||!target.renderFrames)return;
            slots_.ValidateAssignment(previous_.slotId,next_.id,nextType_,nextRuntime_);
            media_=services_.switchSource(previous_.slotId,nextRuntime_,cancellation_.get_token());phase_=Phase::Switching;
        }
        if(phase_==Phase::Switching){
            if(!Completed(media_))return;
            if(cancel_){Restore("Replacement cancelled");return;}
            const auto target=processes_.Get(next_.id);
            if(!target.alive||target.runId!=nextRuntime_||target.retiring)throw std::runtime_error("Replacement Player disappeared before commit");
            retirement_.Commit(slots_,previous_.slotId,{next_.id,nextRuntime_},nextType_);
            phase_=Phase::Complete;waiting_.reset();services_.muteCommands(previous_.slotId,false);
        }
        if(phase_==Phase::Restoring){
            if(!restored_){if(!Completed(media_))return;restored_=true;}
            // Keep preparation-owned processes alive until their data/chat
            // callbacks have settled; they may still be writing routed data.
            if(data_.valid()&&data_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            if(chat_.valid()&&chat_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            // Cancellation preserves preparation writes (including teardown).
            // The save ACK must precede graceful stop; there is no rollback.
            if(started_)retirement_.RetireUnused(slots_,{next_.id,nextRuntime_});
            phase_=Phase::Failed;waiting_.reset();services_.muteCommands(previous_.slotId,false);
        }
    }catch(const std::exception& error){
        if(phase_==Phase::Complete || phase_==Phase::Failed){error_ += std::string("; command routing failed: ")+error.what();}
        else if(phase_==Phase::Restoring){error_ += std::string("; restoration failed: ")+error.what();phase_=Phase::Failed;}
        else try{Restore(error.what());}catch(const std::exception& restoreError){error_=std::string(error.what())+"; restoration failed: "+restoreError.what();phase_=Phase::Failed;}
    }
}
}
