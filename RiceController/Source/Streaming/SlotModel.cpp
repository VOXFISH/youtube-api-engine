#include "SlotModel.h"
#include "../../Shared/Json.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <utility>

namespace RiceController::Streaming {
namespace {
void Id(std::string_view value, const char* field, bool optional = false) {
    if ((value.empty() && !optional) || value.size() > 256 ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 33 || c == 127; }))
        throw std::runtime_error(std::string("Invalid ") + field);
}
void Name(std::string_view value) {
    if (value.empty() || value.size() > 256 ||
        std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; }) ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error("Invalid slot name");
}
void Connection(const YouTubeConnection& value) {
    Id(value.broadcastId, "broadcast ID", true); Id(value.liveChatId, "live chat ID", true);
    Id(value.accountRef, "account reference", true); Id(value.streamCredentialRef, "stream credential reference", true);
}
void ImagePath(const std::string& path) {
    if (path.size() > 32768 || path.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid waiting image path");
}
void RequireIdle(const Slot& slot) {
    if (IsBusy(slot.state)) throw std::runtime_error("Stop the output before changing its connection or encoder profile");
}
int Int(const Json& value) {
    auto number = value.Integer();
    if (number < -2147483647 || number > 2147483647) throw std::runtime_error("Streaming setting integer out of range");
    return static_cast<int>(number);
}
VideoCodec Codec(std::string_view value) {
    if (value == "H264") return VideoCodec::H264;
    if (value == "HEVC") return VideoCodec::HEVC;
    if (value == "AV1") return VideoCodec::AV1;
    throw std::runtime_error("Unknown video codec");
}
LiveLatency Latency(std::string_view value) {
    if (value == "normal") return LiveLatency::Normal;
    if (value == "low") return LiveLatency::Low;
    if (value == "ultraLow") return LiveLatency::UltraLow;
    throw std::runtime_error("Unknown YouTube latency");
}
Json ProfileJson(const OutputProfile& p) {
    return Json::Object{{"width",p.width},{"height",p.height},{"fps",p.fps},{"codec",CodecName(p.codec)},
        {"videoBitrateKbps",p.videoBitrateKbps},{"constantBitrate",p.constantBitrate},{"keyframeSeconds",p.keyframeSeconds},
        {"encoder",p.encoder},{"audioCodec","AAC"},{"audioBitrateKbps",p.audioBitrateKbps},{"audioSampleRate",p.audioSampleRate},
        {"latency",LatencyName(p.latency)},{"streamDelaySeconds",p.streamDelaySeconds},{"autoReconnect",p.autoReconnect},
        {"reconnectAttempts",p.reconnectAttempts},{"reconnectInitialSeconds",p.reconnectInitialSeconds},
        {"keepBroadcastOnSignalLoss",p.keepBroadcastOnSignalLoss}};
}
OutputProfile ReadProfile(const Json& value) {
    OutputProfile p;
    p.width=Int(value.At("width")); p.height=Int(value.At("height")); p.fps=Int(value.At("fps"));
    p.codec=Codec(value.At("codec").String()); p.videoBitrateKbps=Int(value.At("videoBitrateKbps"));
    p.constantBitrate=value.At("constantBitrate").Boolean(); p.keyframeSeconds=Int(value.At("keyframeSeconds"));
    p.encoder=value.At("encoder").String();
    if (value.At("audioCodec").String() != "AAC") throw std::runtime_error("Unsupported audio codec");
    p.audioBitrateKbps=Int(value.At("audioBitrateKbps")); p.audioSampleRate=Int(value.At("audioSampleRate"));
    p.latency=Latency(value.At("latency").String()); p.streamDelaySeconds=Int(value.At("streamDelaySeconds"));
    p.autoReconnect=value.At("autoReconnect").Boolean(); p.reconnectAttempts=Int(value.At("reconnectAttempts"));
    p.reconnectInitialSeconds=Int(value.At("reconnectInitialSeconds"));
    p.keepBroadcastOnSignalLoss=value.At("keepBroadcastOnSignalLoss").Boolean();
    ValidateProfile(p); return p;
}
}
const char* CodecName(VideoCodec codec) {
    switch (codec) { case VideoCodec::H264:return "H264"; case VideoCodec::HEVC:return "HEVC"; case VideoCodec::AV1:return "AV1"; }
    throw std::runtime_error("Invalid video codec");
}
const char* LatencyName(LiveLatency latency) {
    switch (latency) { case LiveLatency::Normal:return "normal"; case LiveLatency::Low:return "low"; case LiveLatency::UltraLow:return "ultraLow"; }
    throw std::runtime_error("Invalid latency");
}
const char* OutputStateName(OutputState state) {
    switch (state) { case OutputState::Idle:return "Idle"; case OutputState::Connecting:return "Connecting";
        case OutputState::Live:return "Live"; case OutputState::Reconnecting:return "Reconnecting"; case OutputState::Failed:return "Failed"; }
    return "Unknown";
}
bool IsActive(OutputState state) { return state == OutputState::Live || state == OutputState::Reconnecting; }
bool IsBusy(OutputState state) { return IsActive(state) || state == OutputState::Connecting; }
void ValidateProfile(const OutputProfile& p) {
    (void)CodecName(p.codec); (void)LatencyName(p.latency);
    if (p.width < 16 || p.height < 16 || p.width > 7680 || p.height > 4320 || p.width % 2 || p.height % 2)
        throw std::runtime_error("Output dimensions must be even and between 16x16 and 7680x4320");
    if (p.fps < 1 || p.fps > 60) throw std::runtime_error("Output FPS must be between 1 and 60");
    if (p.videoBitrateKbps < 100 || p.videoBitrateKbps > 200000 || !p.constantBitrate)
        throw std::runtime_error("YouTube output requires CBR between 100 and 200000 Kbps");
    if (p.keyframeSeconds < 1 || p.keyframeSeconds > 4) throw std::runtime_error("Keyframe interval must be between 1 and 4 seconds");
    Id(p.encoder,"encoder identifier");
    if (p.audioBitrateKbps < 64 || p.audioBitrateKbps > 320 || (p.audioSampleRate != 44100 && p.audioSampleRate != 48000))
        throw std::runtime_error("Invalid AAC audio settings");
    if (p.streamDelaySeconds < 0 || p.streamDelaySeconds > 600) throw std::runtime_error("Stream delay must be between 0 and 600 seconds");
    if (p.reconnectAttempts < 0 || p.reconnectAttempts > 100 || p.reconnectInitialSeconds < 1 || p.reconnectInitialSeconds > 60)
        throw std::runtime_error("Invalid reconnect settings");
    if ((p.width >= 3840 || p.height >= 2160) && p.latency != LiveLatency::Normal)
        throw std::runtime_error("YouTube 4K output requires normal latency");
}
void ValidatePause(const PauseSettings& p) {
    if (p.overlayOpacityPercent < 0 || p.overlayOpacityPercent > 100) throw std::runtime_error("Overlay opacity must be between 0 and 100");
    if (p.overlayPngPath.size() > 32768 || p.overlayPngPath.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid overlay image path");
    if (!p.overlayPngPath.empty()) {
        auto suffix=p.overlayPngPath.size() >= 4 ? p.overlayPngPath.substr(p.overlayPngPath.size()-4) : std::string{};
        std::transform(suffix.begin(),suffix.end(),suffix.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if (suffix != ".png") throw std::runtime_error("Pause overlay must be a PNG image");
    }
}
SlotRegistry::SlotRegistry(EventSink sink):sink_(std::move(sink)) {}
const std::vector<Slot>& SlotRegistry::Slots() const noexcept { return slots_; }
const Slot& SlotRegistry::Get(std::string_view id) const {
    auto it=std::find_if(slots_.begin(),slots_.end(),[&](const Slot& s){return s.slotId==id;});
    if (it==slots_.end()) throw std::runtime_error("Unknown streaming slot");
    return *it;
}
Slot& SlotRegistry::Mutable(std::string_view id) { return const_cast<Slot&>(std::as_const(*this).Get(id)); }
void SlotRegistry::Add(Slot slot) {
    Id(slot.slotId,"slot ID"); Name(slot.name); ValidateProfile(slot.profile); ValidatePause(slot.pause); Connection(slot.connection); ImagePath(slot.waitingImagePath);
    if (slots_.size() >= 1000) throw std::runtime_error("Too many streaming slots");
    if (std::any_of(slots_.begin(),slots_.end(),[&](const Slot& s){return s.slotId==slot.slotId;})) throw std::runtime_error("Duplicate slot ID");
    if (!slot.runtimeInstanceId.empty() || slot.state != OutputState::Idle) throw std::runtime_error("Add configuration before assigning a runtime instance");
    if (slot.entryId.empty() != slot.instanceTypeId.empty()) throw std::runtime_error("Incomplete slot instance identity");
    Id(slot.entryId,"entry ID",true); Id(slot.instanceTypeId,"instance type ID",true);
    if (slot.isMain && slot.entryId.empty()) throw std::runtime_error("An unassigned slot cannot be main");
    if (slot.isMain && std::any_of(slots_.begin(),slots_.end(),[&](const Slot& s){return s.entryId==slot.entryId && s.isMain;}))
        throw std::runtime_error("Duplicate main slot for entry");
    slot.lastError.clear(); slots_.push_back(std::move(slot));
}
void SlotRegistry::CheckMainDeparture(const Slot& slot) const {
    if (!slot.isMain) return;
    if (std::any_of(slots_.begin(),slots_.end(),[&](const Slot& s){return s.slotId!=slot.slotId &&
        ((!slot.runtimeInstanceId.empty() && s.runtimeInstanceId==slot.runtimeInstanceId) ||
         (!slot.entryId.empty() && s.entryId==slot.entryId));}))
        throw std::runtime_error("Choose another main slot before removing or replacing the main slot");
}
void SlotRegistry::Remove(std::string_view id) {
    auto& slot=Mutable(id); RequireIdle(slot); CheckMainDeparture(slot);
    const std::string stableId(id);
    std::erase_if(slots_,[&](const Slot& s){return s.slotId==stableId;});
}
void SlotRegistry::Rename(std::string_view id,std::string name) { Name(name); Mutable(id).name=std::move(name); }
void SlotRegistry::ConfigureInstance(std::string_view id,std::string entry,std::string type) {
    Id(entry,"entry ID"); Id(type,"instance type ID");
    auto& slot=Mutable(id); RequireIdle(slot);
    if (slot.entryId==entry && slot.instanceTypeId==type) return;
    if (!slot.runtimeInstanceId.empty() && slot.entryId==entry && slot.instanceTypeId!=type)
        throw std::runtime_error("A running instance cannot change its stable type identity");
    CheckMainDeparture(slot);
    std::string runtime;
    for (const auto& other:slots_) if (other.slotId!=slot.slotId && other.entryId==entry) {
        if (other.instanceTypeId!=type) throw std::runtime_error("Entry instance type mismatch");
        if (!other.runtimeInstanceId.empty()) runtime=other.runtimeInstanceId;
    }
    slot.isMain=std::none_of(slots_.begin(),slots_.end(),[&](const Slot& s){return s.slotId!=slot.slotId && s.entryId==entry && s.isMain;});
    slot.entryId=std::move(entry); slot.instanceTypeId=std::move(type); slot.runtimeInstanceId=std::move(runtime);
}
void SlotRegistry::BindEntry(std::string_view entry,std::string runtime) {
    Id(entry,"entry ID"); Id(runtime,"runtime instance ID");
    bool found=false;
    for (const auto& slot:slots_) {
        if (slot.entryId==entry) { found=true; if(slot.runtimeInstanceId!=runtime) RequireIdle(slot); }
        else if (slot.runtimeInstanceId==runtime) throw std::runtime_error("Runtime is already assigned to another entry");
    }
    if (!found) throw std::runtime_error("Entry has no streaming slots");
    for (auto& slot:slots_) if (slot.entryId==entry) slot.runtimeInstanceId=runtime;
}
void SlotRegistry::UnbindEntry(std::string_view entry) {
    for (const auto& slot:slots_) if (slot.entryId==entry) RequireIdle(slot);
    for (auto& slot:slots_) if (slot.entryId==entry) slot.runtimeInstanceId.clear();
}
void SlotRegistry::SetInstanceType(std::string_view entry,std::string type) {
    Id(type,"instance type ID");
    for(const auto& slot:slots_) if(slot.entryId==entry) {
        RequireIdle(slot);
        if(!slot.runtimeInstanceId.empty()) throw std::runtime_error("Stop the game before changing its data identity");
    }
    for(auto& slot:slots_) if(slot.entryId==entry) slot.instanceTypeId=type;
}
void SlotRegistry::CheckOwnership(const Slot& candidate) const {
    for (const auto& other:slots_) {
        if (other.slotId==candidate.slotId || !IsBusy(other.state)) continue;
        if (!candidate.connection.broadcastId.empty() && candidate.connection.broadcastId==other.connection.broadcastId)
            throw std::runtime_error("A broadcast already has an active slot output");
        if (!candidate.connection.streamCredentialRef.empty() && candidate.connection.streamCredentialRef==other.connection.streamCredentialRef)
            throw std::runtime_error("An ingest credential already has an active slot output");
        if (!candidate.connection.liveChatId.empty() && candidate.connection.liveChatId==other.connection.liveChatId &&
            candidate.runtimeInstanceId != other.runtimeInstanceId)
            throw std::runtime_error("One live chat cannot control different runtime instances");
    }
}
void SlotRegistry::Assign(std::string_view id,std::string entry,std::string type,std::string runtime) {
    Id(entry,"entry ID"); Id(type,"instance type ID"); Id(runtime,"runtime instance ID");
    auto& slot=Mutable(id);
    if (slot.entryId==entry && slot.instanceTypeId==type && slot.runtimeInstanceId==runtime) return;
    if (slot.runtimeInstanceId==runtime && (slot.entryId!=entry || slot.instanceTypeId!=type))
        throw std::runtime_error("A runtime instance cannot change its entry or stable type identity");
    if (slot.entryId!=entry || (!slot.runtimeInstanceId.empty() && slot.runtimeInstanceId!=runtime)) CheckMainDeparture(slot);
    for (const auto& other:slots_) {
        if (other.slotId==slot.slotId) continue;
        if (other.runtimeInstanceId==runtime && (other.entryId!=entry || other.instanceTypeId!=type))
            throw std::runtime_error("Runtime instance identity does not match its entry and type");
        if (other.entryId==entry && (!other.runtimeInstanceId.empty() && other.runtimeInstanceId!=runtime))
            throw std::runtime_error("An entry is already attached to another runtime instance");
        if (other.entryId==entry && other.instanceTypeId!=type) throw std::runtime_error("Entry instance type mismatch");
    }
    Slot candidate=slot; candidate.entryId=std::move(entry); candidate.instanceTypeId=std::move(type); candidate.runtimeInstanceId=std::move(runtime);
    candidate.isMain=std::none_of(slots_.begin(),slots_.end(),[&](const Slot& s){return s.slotId!=slot.slotId && s.entryId==candidate.entryId && s.isMain;});
    if (IsBusy(candidate.state)) {
        if (!candidate.isMain && MainSlotFor(candidate.runtimeInstanceId).empty())
            throw std::runtime_error("Bind the target main slot before replacing an active output's source");
        CheckOwnership(candidate);
    }
    const auto oldRuntime=slot.runtimeInstanceId, oldMain=MainSlotFor(oldRuntime);
    const bool active=IsActive(slot.state);
    const bool oldEnds=active && !oldRuntime.empty() && oldRuntime!=candidate.runtimeInstanceId && ActiveCount(oldRuntime)==1;
    const bool newStarts=active && oldRuntime!=candidate.runtimeInstanceId && ActiveCount(candidate.runtimeInstanceId)==0;
    slot=std::move(candidate);
    if (oldEnds) Emit(StreamEventKind::End,oldRuntime,oldMain);
    if (newStarts) Emit(StreamEventKind::Start,slot.runtimeInstanceId,MainSlotFor(slot.runtimeInstanceId));
}
void SlotRegistry::ValidateAssignment(std::string_view id,const std::string& entry,const std::string& type,const std::string& runtime) const {
    auto candidate=*this;candidate.sink_={};candidate.Assign(id,entry,type,runtime);
}
void SlotRegistry::ClearAssignment(std::string_view id) {
    auto& slot=Mutable(id); RequireIdle(slot); CheckMainDeparture(slot);
    slot.entryId.clear(); slot.instanceTypeId.clear(); slot.runtimeInstanceId.clear(); slot.isMain=false;
}
void SlotRegistry::SetMain(std::string_view id) {
    auto& selected=Mutable(id);
    if (selected.entryId.empty()) throw std::runtime_error("Assign an instance before selecting a main slot");
    if (selected.isMain) return;
    for (auto& slot:slots_) if (slot.entryId==selected.entryId) slot.isMain=false;
    selected.isMain=true;
    if (!selected.runtimeInstanceId.empty()) Emit(StreamEventKind::MainSlotChanged,selected.runtimeInstanceId,selected.slotId);
}
void SlotRegistry::SetProfile(std::string_view id,OutputProfile profile) { ValidateProfile(profile); auto& slot=Mutable(id); RequireIdle(slot); slot.profile=std::move(profile); }
void SlotRegistry::SetPause(std::string_view id,PauseSettings settings) { ValidatePause(settings); Mutable(id).pause=std::move(settings); }
void SlotRegistry::SetWaitingImage(std::string_view id,std::string path) { ImagePath(path); Mutable(id).waitingImagePath=std::move(path); }
void SlotRegistry::SetConnection(std::string_view id,YouTubeConnection connection) { Connection(connection); auto& slot=Mutable(id); RequireIdle(slot); slot.connection=std::move(connection); }
void SlotRegistry::MarkConnecting(std::string_view id) {
    auto& slot=Mutable(id); RequireIdle(slot);
    if (slot.runtimeInstanceId.empty() || MainSlotFor(slot.runtimeInstanceId).empty()) throw std::runtime_error("Assign an instance and main slot before connecting");
    if (slot.connection.broadcastId.empty() || slot.connection.liveChatId.empty() || slot.connection.streamCredentialRef.empty() || slot.connection.accountRef.empty())
        throw std::runtime_error("Complete the YouTube connection before connecting");
    if (std::count_if(slots_.begin(),slots_.end(),[](const Slot& s){return IsBusy(s.state);}) >= static_cast<std::ptrdiff_t>(MaxConcurrentOutputs))
        throw std::runtime_error("At most five outputs may connect or stream simultaneously");
    CheckOwnership(slot); slot.state=OutputState::Connecting; slot.lastError.clear();
}
void SlotRegistry::MarkLive(std::string_view id) {
    auto& slot=Mutable(id);
    if (slot.state==OutputState::Live) return;
    if (slot.state!=OutputState::Connecting && slot.state!=OutputState::Reconnecting) throw std::runtime_error("Output has not connected");
    CheckOwnership(slot); bool first=ActiveCount(slot.runtimeInstanceId)==0; slot.state=OutputState::Live; slot.lastError.clear();
    if (first) Emit(StreamEventKind::Start,slot.runtimeInstanceId,MainSlotFor(slot.runtimeInstanceId));
}
void SlotRegistry::MarkReconnecting(std::string_view id,std::string error) {
    auto& slot=Mutable(id);
    if (!IsActive(slot.state)) throw std::runtime_error("Only a started output can reconnect");
    if (!slot.profile.autoReconnect) throw std::runtime_error("Automatic reconnection is disabled");
    slot.state=OutputState::Reconnecting; slot.lastError=std::move(error);
}
void SlotRegistry::MarkStopped(std::string_view id,std::string error) {
    auto& slot=Mutable(id); bool last=IsActive(slot.state) && ActiveCount(slot.runtimeInstanceId)==1;
    slot.state=error.empty() ? OutputState::Idle : OutputState::Failed; slot.lastError=std::move(error);
    if (last) Emit(StreamEventKind::End,slot.runtimeInstanceId,MainSlotFor(slot.runtimeInstanceId));
}
void SlotRegistry::StopAll() { for (auto& slot:slots_) MarkStopped(slot.slotId); }
std::size_t SlotRegistry::ActiveCount(std::string_view id) const {
    if (id.empty()) return 0;
    return static_cast<std::size_t>(std::count_if(slots_.begin(),slots_.end(),[&](const Slot& s){return s.runtimeInstanceId==id && IsActive(s.state);}));
}
std::string SlotRegistry::MainSlotFor(std::string_view id) const {
    if (id.empty()) return {};
    for (const auto& slot:slots_) if (slot.runtimeInstanceId==id && slot.isMain) return slot.slotId;
    return {};
}
void SlotRegistry::Emit(StreamEventKind kind,const std::string& runtime,const std::string& main) const noexcept {
    // Observers must not unwind a committed state transition. UI/log adapters
    // should report their own errors and must not mutate the registry reentrantly.
    if (sink_) try { sink_({kind,runtime,main}); } catch (...) {}
}
std::string SlotRegistry::Serialize() const {
    Json::Array items;
    for (const auto& slot:slots_) {
        items.emplace_back(Json::Object{{"slotId",slot.slotId},{"name",slot.name},{"entryId",slot.entryId},
            {"instanceTypeId",slot.instanceTypeId},{"isMain",slot.isMain},{"profile",ProfileJson(slot.profile)},
            {"waitingImagePath",slot.waitingImagePath},
            {"pause",Json::Object{{"stopOutput",slot.pause.stopOutput},{"muteGameAudio",slot.pause.muteGameAudio},
                {"overlayPngPath",slot.pause.overlayPngPath},{"overlayOpacityPercent",slot.pause.overlayOpacityPercent}}},
            {"connection",Json::Object{{"broadcastId",slot.connection.broadcastId},{"liveChatId",slot.connection.liveChatId},
                {"accountRef",slot.connection.accountRef},{"streamCredentialRef",slot.connection.streamCredentialRef}}}});
    }
    return Json(Json::Object{{"schemaVersion",1},{"slots",std::move(items)}}).Dump()+"\n";
}
SlotRegistry SlotRegistry::Deserialize(std::string_view text,EventSink sink) {
    auto root=Json::Parse(text);
    if (root.At("schemaVersion").Integer()!=1) throw std::runtime_error("Unsupported streaming settings version");
    SlotRegistry result(std::move(sink));
    for (const auto& item:root.At("slots").Items()) {
        Slot slot; slot.slotId=item.At("slotId").String(); slot.name=item.At("name").String();
        slot.entryId=item.At("entryId").String(); slot.instanceTypeId=item.At("instanceTypeId").String();
        slot.isMain=item.At("isMain").Boolean(); slot.profile=ReadProfile(item.At("profile"));
        if (const auto* image=item.Find("waitingImagePath")) slot.waitingImagePath=image->String();
        const auto& pause=item.At("pause"); slot.pause.stopOutput=pause.At("stopOutput").Boolean();
        slot.pause.muteGameAudio=pause.At("muteGameAudio").Boolean(); slot.pause.overlayPngPath=pause.At("overlayPngPath").String();
        slot.pause.overlayOpacityPercent=Int(pause.At("overlayOpacityPercent"));
        const auto& connection=item.At("connection"); slot.connection.broadcastId=connection.At("broadcastId").String();
        slot.connection.liveChatId=connection.At("liveChatId").String(); slot.connection.accountRef=connection.At("accountRef").String();
        slot.connection.streamCredentialRef=connection.At("streamCredentialRef").String(); result.Add(std::move(slot));
    }
    // Validate saved groups even when the main appears after a secondary slot.
    for (const auto& slot:result.slots_) if (!slot.entryId.empty()) {
        if (std::none_of(result.slots_.begin(),result.slots_.end(),[&](const Slot& s){return s.entryId==slot.entryId && s.isMain;}))
            throw std::runtime_error("Saved slot group has no main slot");
        for (const auto& other:result.slots_) if (other.entryId==slot.entryId && other.instanceTypeId!=slot.instanceTypeId)
            throw std::runtime_error("Saved slot group has mismatched instance types");
    }
    return result;
}
}
