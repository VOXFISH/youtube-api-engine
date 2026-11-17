#include "Streaming/SlotModel.h"
#include "Streaming/ApiKeyPool.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace RiceController::Streaming;
namespace {
void Check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void Reject(F operation,const char* message) {
    bool rejected=false; try { operation(); } catch(const std::exception&) { rejected=true; }
    Check(rejected,message);
}
Slot MakeSlot(std::string id) {
    Slot slot; slot.slotId=id; slot.name="Slot "+id;
    slot.connection={"broadcast-"+id,"chat-"+id,"account-1","ingest-ref-"+id};
    return slot;
}
void Live(SlotRegistry& model,const std::string& id) { model.MarkConnecting(id); model.MarkLive(id); }
void DefaultsAndValidation() {
    OutputProfile p; ValidateProfile(p);
    Check(p.width==1920 && p.height==1080 && p.fps==60 && p.videoBitrateKbps==17000,"1080p60 H264 default");
    Check(p.codec==VideoCodec::H264 && p.constantBitrate && p.audioBitrateKbps==128 && p.audioSampleRate==48000,"CBR and AAC default");
    PauseSettings pause; Check(!pause.stopOutput && pause.muteGameAudio,"Pause preserves broadcast with silent game audio");
    pause.overlayPngPath="C:/images/overlay.PNG"; pause.overlayOpacityPercent=35; ValidatePause(pause);
    pause.overlayOpacityPercent=101; Reject([&]{ValidatePause(pause);},"Invalid alpha rejected");
    pause.overlayOpacityPercent=50; pause.overlayPngPath="image.jpg"; Reject([&]{ValidatePause(pause);},"Non-PNG rejected");
    p.fps=61; Reject([&]{ValidateProfile(p);},"Unsupported FPS rejected");
    p=OutputProfile{}; p.width=1919; Reject([&]{ValidateProfile(p);},"Odd width rejected");
    p=OutputProfile{}; p.constantBitrate=false; Reject([&]{ValidateProfile(p);},"Non-CBR rejected");
    p=OutputProfile{}; p.width=3840; p.height=2160;
    Reject([&]{ValidateProfile(p);},"4K low latency rejected"); p.latency=LiveLatency::Normal; ValidateProfile(p);
    p=OutputProfile{}; p.codec=static_cast<VideoCodec>(99); Reject([&]{ValidateProfile(p);},"Invalid codec enum rejected");
}
void SharedInstanceLifecycle() {
    std::vector<StreamEvent> events;
    SlotRegistry model([&](const StreamEvent& e){events.push_back(e);});
    model.Add(MakeSlot("a")); model.Add(MakeSlot("b"));
    model.ConfigureInstance("a","entry-1","game-one"); model.ConfigureInstance("b","entry-1","game-one");
    Check(model.Get("a").isMain && !model.Get("b").isMain,"First assignment chooses main");
    Reject([&]{model.MarkConnecting("a");},"No fake start before runtime exists");
    model.BindEntry("entry-1","run-1");
    model.MarkConnecting("a"); Check(events.empty() && model.ActiveCount("run-1")==0,"Connecting does not emit OnStreamStart");
    model.MarkLive("a"); Live(model,"b");
    Check(events.size()==1 && events[0].kind==StreamEventKind::Start && model.ActiveCount("run-1")==2,"Shared instance starts once");
    model.MarkReconnecting("a","network loss");
    Check(model.ActiveCount("run-1")==2 && events.size()==1,"Temporary signal loss does not end logical stream");
    model.MarkLive("a"); Check(events.size()==1,"Reconnect does not repeat start");
    model.MarkStopped("a");
    Check(model.ActiveCount("run-1")==1 && model.MainSlotFor("run-1")=="a" && events.size()==1,"Stopped main retains slot data anchor");
    Reject([&]{model.Remove("a");},"Main cannot disappear silently while group exists");
    model.SetMain("b"); Check(events.back().kind==StreamEventKind::MainSlotChanged,"Main change is observable");
    model.Remove("a"); model.MarkStopped("b");
    Check(events.back().kind==StreamEventKind::End && events.back().mainSlotId=="b","Only final output ends instance stream");
    auto count=events.size(); model.MarkStopped("b"); Check(events.size()==count,"Repeated stop is idempotent");
    model.UnbindEntry("entry-1"); Check(model.Get("b").runtimeInstanceId.empty() && model.Get("b").entryId=="entry-1","Process detach preserves slot configuration");
    model.SetInstanceType("entry-1","game-one-v2"); Check(model.Get("b").instanceTypeId=="game-one-v2","Stopped game can explicitly select another data identity");
}
void ReplacementAndOwnership() {
    std::vector<StreamEvent> events;
    SlotRegistry model([&](const StreamEvent& e){events.push_back(e);});
    for(auto id:{"a","b","c"}) model.Add(MakeSlot(id));
    model.Assign("a","entry-1","game-one","run-1"); model.Assign("b","entry-1","game-one","run-1");
    Live(model,"a"); Live(model,"b");
    Reject([&]{model.Assign("a","entry-2","game-two","run-2");},"Replacing main requires explicit handoff");
    model.Assign("b","entry-2","game-two","run-2");
    Check(model.Get("a").state==OutputState::Live && model.Get("b").state==OutputState::Live,"Replacing source does not interrupt other output");
    Check(model.ActiveCount("run-1")==1 && model.ActiveCount("run-2")==1 && events.back().runtimeInstanceId=="run-2","Replacement updates per-instance reference counts");
    auto connection=model.Get("c").connection; connection.liveChatId="chat-a"; model.SetConnection("c",connection);
    model.Assign("c","entry-3","game-three","run-3");
    Reject([&]{model.MarkConnecting("c");},"One chat cannot control different live instances");
    Check(model.Get("c").state==OutputState::Idle,"Rejected connection leaves no partial state");
    connection.liveChatId="chat-c"; connection.broadcastId="broadcast-a"; model.SetConnection("c",connection);
    Reject([&]{model.MarkConnecting("c");},"One broadcast cannot have competing output transports");
    connection.broadcastId="broadcast-c"; connection.streamCredentialRef="ingest-ref-a"; model.SetConnection("c",connection);
    Reject([&]{model.MarkConnecting("c");},"Ingest credential cannot be used concurrently by two outputs");
    Reject([&]{model.SetProfile("a",OutputProfile{});},"Live profile cannot silently rebuild encoder");
    Reject([&]{model.BindEntry("entry-3","run-1");},"Runtime IDs cannot identify two entries");
    Reject([&]{model.Assign("b","entry-2","changed-type","run-2");},"A runtime cannot silently change its stable game type");
    model.StopAll(); Check(model.ActiveCount("run-1")==0 && model.ActiveCount("run-2")==0,"Controller shutdown clears every active output");
    Reject([&]{model.ConfigureInstance("b","entry-2","changed-type");},"Reconfiguration cannot mutate a bound runtime type");
}
void CapacityAndPersistence() {
    SlotRegistry model;
    for(int i=0;i<6;++i) {
        auto id=std::to_string(i); model.Add(MakeSlot(id)); model.ConfigureInstance(id,"entry","stable-type");
    }
    model.BindEntry("entry","run-before-restart");
    for(int i=0;i<5;++i) Live(model,std::to_string(i));
    Reject([&]{model.MarkConnecting("5");},"Sixth output exceeds five-output capacity");
    auto pause=model.Get("2").pause; pause.overlayPngPath="C:/images/일시정지.png"; pause.overlayOpacityPercent=37;
    model.SetPause("2",pause);
    model.SetWaitingImage("2","C:/images/대기.png");
    model.SetMain("3"); auto text=model.Serialize();
    Check(text.find("run-before-restart")==std::string::npos,"Runtime IDs are not persisted");
    auto restored=SlotRegistry::Deserialize(text);
    Check(restored.Get("2").waitingImagePath=="C:/images/대기.png","Replacement waiting image survives restart");
    Check(restored.Slots().size()==6 && restored.Get("3").isMain && restored.Get("2").pause==pause,"Settings, alpha and selected main survive restart");
    for(const auto& slot:restored.Slots()) Check(slot.state==OutputState::Idle && slot.runtimeInstanceId.empty(),"Saved output is never presented as broadcasting");
    restored.SetInstanceType("entry","changed-type");
    for(const auto& slot:restored.Slots())Check(slot.instanceTypeId=="changed-type","Data identity changes apply to the whole shared entry");
    restored.BindEntry("entry","new-run"); Check(restored.MainSlotFor("new-run")=="3","Stored main binds to new process");
    Reject([&]{SlotRegistry::Deserialize("{\"schemaVersion\":8,\"slots\":[]}");},"Unsupported schema rejected");
    Reject([&]{model.Add(MakeSlot("0"));},"Stable slot IDs are unique");
    model.MarkStopped("0"); Live(model,"5"); Check(model.ActiveCount("run-before-restart")==5,"Freed output capacity can be reused");
}
void RejectedReplacementIsAtomic() {
    std::vector<StreamEvent> events;SlotRegistry model([&](const StreamEvent& event){events.push_back(event);});
    model.Add(MakeSlot("source"));model.Add(MakeSlot("target-main"));
    model.Assign("source","old-entry","old-type","old-run");Live(model,"source");
    model.ConfigureInstance("target-main","new-entry","new-type");
    const auto before=model.Serialize();const auto eventCount=events.size();
    Reject([&]{model.Assign("source","new-entry","new-type","new-run");},"Active replacement cannot enter a group whose main has no runtime");
    Check(model.Serialize()==before && model.ActiveCount("old-run")==1 && model.ActiveCount("new-run")==0 && events.size()==eventCount,
        "Rejected replacement changes neither assignment, live counts nor stream callbacks");
    model.BindEntry("new-entry","new-run");model.Assign("source","new-entry","new-type","new-run");
    Check(model.Get("source").state==OutputState::Live && model.MainSlotFor("new-run")=="target-main" && model.ActiveCount("old-run")==0,
        "Prepared replacement retains output with a valid target data anchor");
    Check(events[events.size()-2].kind==StreamEventKind::End && events.back().kind==StreamEventKind::Start && events.back().mainSlotId=="target-main",
        "Committed replacement emits old end and new start with stable main slot");
    SlotRegistry initial;initial.Add(MakeSlot("a"));initial.Add(MakeSlot("b"));
    initial.Assign("a","entry","type","run");initial.Assign("b","entry","type","run");
    initial.MarkConnecting("a");initial.MarkConnecting("b");initial.MarkStopped("a");
    Check(initial.ActiveCount("run")==0,"Stopping an initial connection cannot emit a logical stream end");
    initial.MarkLive("b");Check(initial.ActiveCount("run")==1,"Other initial output can still establish the instance stream");
}
void ApiKeyFailures() {
    ApiKeyPool pool("project-one");
    pool.Add({"a","project-one","vault-a"}); pool.Add({"b","project-one","vault-b"}); pool.Add({"c","project-one","vault-c"});
    Reject([&]{pool.Add({"other","project-two","vault-other"});},"Quota groups cannot be pooled for automatic failover");
    Check(pool.Current().key->keyId=="a" && pool.Current().key->keyId=="a","Successful requests do not rotate keys");
    auto result=pool.ReportFailure("a",ApiKeyFailure::Network);
    Check(result.action==KeyAction::RetryLater && result.key->keyId=="a","Network failure backs off on same key");
    result=pool.ReportFailure("a",ApiKeyFailure::InvalidKey);
    Check(result.action==KeyAction::UseReplacement && result.key->keyId=="b","Credential failure rotates once");
    result=pool.ReportFailure("a",ApiKeyFailure::InvalidKey);
    Check(result.action==KeyAction::UseCurrent && result.key->keyId=="b","Late failure from old request does not rotate current key");
    result=pool.ReportFailure("b",ApiKeyFailure::RateLimited);
    Check(result.action==KeyAction::RetryLater && result.key->keyId=="b","Rate limit never triggers rotation");
    result=pool.ReportFailure("b",ApiKeyFailure::QuotaExceeded);
    Check(result.action==KeyAction::Stop && pool.QuotaExhausted() && !pool.Current().key,"Quota exhaustion stops all same-project keys");
    pool.SetEnabled("c",true); Check(!pool.Current().key,"Enabling a key cannot bypass exhausted project quota");
    pool.ConfirmQuotaReset(); Check(pool.Current().key->keyId=="b","Confirmed reset preserves current valid key");
    result=pool.ReportFailure("b",ApiKeyFailure::Unauthorized); Check(result.action==KeyAction::Stop,"OAuth errors are not API-key rotation");
    Reject([&]{pool.ReportFailure("missing",ApiKeyFailure::InvalidKey);},"Unknown response credential rejected");
}
}
int main() {
    try {
        DefaultsAndValidation(); SharedInstanceLifecycle(); ReplacementAndOwnership(); CapacityAndPersistence(); RejectedReplacementIsAtomic(); ApiKeyFailures();
        std::cout<<"Streaming slot and API key invariants passed\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
