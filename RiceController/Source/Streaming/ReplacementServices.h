#pragma once
#include "GameReplacementFlow.h"
#include "PlayerOutput.h"
#include "SlotChatSession.h"
namespace RiceController::Streaming {
// Connects the real media worker, Player restore IPC and YouTube polling worker.
// invalidateQueuedCommands must synchronously gate commands already delivered
// to the owner thread, including the per-slot receiver epoch/cutoff.
inline ReplacementServices ConnectReplacementServices(PlayerOutput& output,ProcessManager& processes,
    SlotChatSession& chat,std::function<void(const std::string&,bool)> invalidateQueuedCommands){
    if(!invalidateQueuedCommands)throw std::invalid_argument("A queued-command gate is required");
    ReplacementServices services;
    services.showWaiting=[&output](const std::string&,auto image,std::stop_token token){return output.ShowWaiting(std::move(image),token);};
    services.switchSource=[&output](const std::string&,const std::string& runtime,std::stop_token token){return output.SwitchSource(runtime,token);};
    services.restoreData=[&processes](const std::string& main,const std::string& runtime,std::stop_token token){
        if(token.stop_requested())throw std::runtime_error("Slot restore cancelled");
        return processes.RestoreSlotForRun(runtime,main);
    };
    services.connectChat=[&chat](const std::string&,const std::string&,std::stop_token token){return chat.Ready(token);};
    services.muteCommands=[&chat,gate=std::move(invalidateQueuedCommands)](const std::string& slot,bool muted){
        chat.SetMuted(muted);gate(slot,muted);
    };
    return services;
}
