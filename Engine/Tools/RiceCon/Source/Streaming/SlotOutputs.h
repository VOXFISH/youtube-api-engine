#pragma once
#include "PlayerOutput.h"
#include "../ProcessManager.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <map>

namespace RiceController::Streaming {
// The UI thread owns the workers. A slot becomes live after its first encoded
// packet reaches the destination, and remains owned until shutdown completes.
class SlotOutputs {
public:
    using Open = std::function<std::unique_ptr<PlayerOutput>(const Slot&)>;
    explicit SlotOutputs(Open open):open_(std::move(open)) {}
    ~SlotOutputs(){StopAll();}
    bool Empty() const {return sessions_.empty();}
    bool Stopping(const std::string& id) const {
        auto it=sessions_.find(id);
        return it!=sessions_.end()&&it->second.stopping;
    }
    void Start(SlotRegistry& registry,const std::string& id){
        if(sessions_.contains(id))throw std::runtime_error("이미 이 슬롯에서 송출을 처리하고 있습니다.");
        registry.MarkConnecting(id);
        try{
            OpenWorker(sessions_[id],registry.Get(id));
        }catch(const std::exception& e){
            sessions_.erase(id);
            registry.MarkStopped(id,e.what());
            throw;
        }
    }
    void Stop(const std::string& id){
        auto it=sessions_.find(id);
        if(it!=sessions_.end()){it->second.failure.clear();it->second.Stop();}
    }
    void StopAll(){for(auto& [id,session]:sessions_){session.failure.clear();session.Stop();}}
    void Poll(SlotRegistry& registry,const ProcessManager& processes){
        using namespace std::chrono;
        for(auto it=sessions_.begin();it!=sessions_.end();){
            const auto& slot=registry.Get(it->first);
            auto& session=it->second;
            const Status status=processes.Get(slot.entryId);
            if(!status.alive || status.runId!=slot.runtimeInstanceId){
                if(!session.stopping)session.failure="게임 인스턴스가 종료되었습니다.";
                session.Stop();
            }else if(slot.pause.stopOutput&&status.state==RunState::Paused)session.Stop();
            if(!session.stopping&&!session.output&&Clock::now()>=session.retryAt){
                try{OpenWorker(session,slot);}
                catch(const std::exception& e){session.failure=e.what();session.Stop();}
            }
            if(!session.stopping&&session.started.valid()&&session.started.wait_for(seconds(0))==std::future_status::ready){
                try{session.started.get();registry.MarkLive(slot.slotId);session.attempts=0;}
                catch(const std::exception& e){session.failure=e.what();}
            }
            if(session.output&&session.output->Finished()){
                if(!session.stopping){
                    session.failure=session.output->Error();
                    if(session.failure.empty())session.failure="송출 연결이 끊어졌습니다.";
                }
                session.output.reset();
                if(!session.stopping&&IsActive(slot.state)&&slot.profile.autoReconnect&&session.attempts<slot.profile.reconnectAttempts){
                    registry.MarkReconnecting(slot.slotId,session.failure);
                    const int delay=std::min(60,slot.profile.reconnectInitialSeconds*(1<<std::min(session.attempts,6)));
                    session.retryAt=Clock::now()+seconds(delay);
                    ++session.attempts;
                }else session.stopping=true;
            }
            if(session.stopping&&!session.output){
                registry.MarkStopped(slot.slotId,session.failure);
                it=sessions_.erase(it);
            }else ++it;
        }
    }
private:
    using Clock=std::chrono::steady_clock;
    struct Session{
        std::unique_ptr<PlayerOutput> output;
        std::future<void> started;
        std::string failure;
        int attempts=0;
        bool stopping=false;
        Clock::time_point retryAt;
        void Stop(){stopping=true;if(output)output->RequestStop();}
    };
    void OpenWorker(Session& session,const Slot& slot){
        session.output=open_(slot);
        session.started=session.output->Started();
        session.failure.clear();
    }
    std::map<std::string,Session> sessions_;
    Open open_;
};
}
