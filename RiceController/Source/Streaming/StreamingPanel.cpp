#include "StreamingPanel.h"
#include "SlotModel.h"
#include "GenericDataEditor.h"
#include "Engine/Data/DataValueCodec.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <future>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <unordered_map>

namespace RiceController::Streaming {
namespace D=Rice::Data;
namespace {
D::RouteKey SettingsKey() {
    D::RouteKey key; key.domain="controller"; key.build="controller"; key.dataId="streaming.slots"; return key;
}
const Entry* FindEntry(const std::vector<Entry>& entries,const std::string& id) {
    auto found=std::find_if(entries.begin(),entries.end(),[&](const Entry& entry){return entry.id==id;});
    return found==entries.end()?nullptr:&*found;
}
std::optional<std::string> EditText(const D::EncodedValue& value) {
    return GenericDataEditor::ToText(value);
}
D::EncodedValue EncodeText(const std::string& type,uint32_t version,const std::string& text) {
    return GenericDataEditor::FromText(type,version,text);
}
}
struct StreamingPanel::Impl {
    enum class Operation { Load, SaveSettings, ReadData, WriteData };
    struct Result {
        std::shared_ptr<D::SqliteStore> store;
        std::optional<D::DataRecord> settings;
        std::vector<D::DataRecord> records;
    };
    std::filesystem::path database;
    std::shared_ptr<D::SqliteStore> store;
    SlotRegistry registry;
    std::optional<SlotRegistry> pendingRegistry;
    std::string pendingManagedSave;
    int64_t settingsRevision=0;
    std::future<Result> task;
    Operation operation=Operation::Load;
    bool ready=false, healthy=true, managementOpen=false, editingData=false, addingData=false;
    std::string error,info,managedId,dataBuild,dataFilter,deleteSlotId;
    Slot draft;
    std::vector<D::DataRecord> records;
    std::unordered_map<std::string,std::pair<int64_t,std::string>> valuePreviews;
    D::DataRecord dataDraft;
    std::string dataText, valueReadOnlyReason;
    bool editableValue=true,allGameData=false;
    int newType=0;
    static constexpr const char* Types[]={"utf8","i64","f64","bool","i32","u32","u64","f32",
        "vec2(f32)","vec3(f32)","vec4(f32)","list(i32:1)","list(f32:1)","list(utf8:1)","array(3,f32:1)"};

    explicit Impl(std::filesystem::path path):database(std::move(path)) { Load(); }
    bool Busy()const{return task.valid();}
    template<class F> void Guard(F&& action) { try{action();}catch(const std::exception& failure){error=failure.what();} }
    template<class F> void Launch(Operation kind,F&& fn) {
        if(Busy())throw std::runtime_error("이전 저장 작업이 끝날 때까지 기다려 주세요.");
        error.clear(); operation=kind; task=std::async(std::launch::async,std::forward<F>(fn));
    }
    void Load() {
        const auto path=database;
        Launch(Operation::Load,[path] {
            if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
            Result result; result.store=std::make_shared<D::SqliteStore>(path); result.settings=result.store->Get(SettingsKey()); return result;
        });
    }
    void Poll() {
        if(!Busy() || task.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        try {
            auto result=task.get();
            if(operation==Operation::Load) {
                auto restored=result.settings ? SlotRegistry::Deserialize(D::DecodeValue<std::string>(result.settings->value)) : SlotRegistry{};
                store=std::move(result.store); registry=std::move(restored);
                settingsRevision=result.settings ? result.settings->revision : 0;
                ready=true; healthy=true; managementOpen=false;pendingManagedSave.clear();
            } else if(operation==Operation::SaveSettings) {
                registry=std::move(*pendingRegistry); pendingRegistry.reset(); settingsRevision=result.settings->revision;
                info="슬롯 설정을 저장했습니다.";
                if(managementOpen && pendingManagedSave==managedId)draft=registry.Get(managedId);
                pendingManagedSave.clear();
            } else {
                records=std::move(result.records);
                if(operation==Operation::WriteData){editingData=false; info="데이터 변경을 저장했습니다.";}
            }
        } catch(const std::exception& failure) {
            error=failure.what(); pendingRegistry.reset();pendingManagedSave.clear();
            if(operation==Operation::Load){healthy=false;ready=false;}
        }
    }
    void Save(SlotRegistry next,bool refreshManagement=false) {
        if(Busy())throw std::runtime_error("이전 저장 작업이 끝날 때까지 기다려 주세요.");
        if(!ready || !healthy)throw std::runtime_error("저장소를 먼저 정상적으로 읽어야 합니다.");
        D::DataRecord value; value.key=SettingsKey(); value.name="Streaming slots"; value.value=D::EncodeValue(next.Serialize());
        const auto revision=settingsRevision; const auto db=store;
        pendingRegistry=std::move(next);
        pendingManagedSave=refreshManagement?managedId:std::string{};
        Launch(Operation::SaveSettings,[db,value=std::move(value),revision] { Result result; result.settings=db->Put(value,revision); return result; });
    }
    void ReadData() {
        if(Busy())throw std::runtime_error("이전 저장 작업이 끝날 때까지 기다려 주세요.");
        records.clear();valuePreviews.clear();editingData=false;
        if(managedId.empty() || (!allGameData && dataBuild.empty()))return;
        const auto db=store; const auto slot=managedId,build=dataBuild; const bool all=allGameData;
        Launch(Operation::ReadData,[db,slot,build,all] { Result result; result.records=all?db->ListSlotAll("local",slot):db->ListSlot("local",slot,build); return result; });
    }
    void Manage(const std::string& id) {
        if(Busy())throw std::runtime_error("이전 저장 작업이 끝날 때까지 기다려 주세요.");
        draft=registry.Get(id); managedId=id; managementOpen=true; dataBuild=draft.instanceTypeId;
        dataFilter=dataBuild; allGameData=false; records.clear(); editingData=false; ReadData();
    }
    void WriteData(D::DataRecord value,bool erase=false) {
        if(value.key.domain!="local" || value.key.slot!=managedId || value.key.lifetime!=D::Lifetime::Slot || value.key.scope!=D::Scope::Build ||
           (!allGameData && value.key.build!=dataBuild))throw std::runtime_error("현재 데이터 관리 범위와 항목이 일치하지 않습니다.");
        const auto db=store; const auto slot=managedId,build=dataBuild; const bool all=allGameData;
        Launch(Operation::WriteData,[db,slot,build,all,value=std::move(value),erase] {
            if(erase)db->Erase(value.key,value.revision);else db->Put(value,value.revision);
            Result result; result.records=all?db->ListSlotAll("local",slot):db->ListSlot("local",slot,build); return result;
        });
    }
    void EditData(const D::DataRecord& value) {
        dataDraft=value; valueReadOnlyReason.clear();
        try {
            auto decoded=EditText(value.value); editableValue=decoded.has_value(); dataText=decoded.value_or("");
            if(!editableValue)valueReadOnlyReason="등록된 값 스키마가 없어 읽기 전용입니다. 이름과 설명은 수정할 수 있습니다.";
        } catch(const std::exception& failure) {
            editableValue=false; dataText.clear(); valueReadOnlyReason=std::string("값 편집 불가: ")+failure.what()+" 이름과 설명은 수정할 수 있습니다.";
        }
        addingData=false; editingData=true;
    }
    void AddData() {
        dataDraft={}; dataDraft.key.domain="local"; dataDraft.key.lifetime=D::Lifetime::Slot;
        dataDraft.key.slot=managedId; dataDraft.key.build=allGameData?dataFilter:dataBuild; dataDraft.value=D::EncodeValue(std::string{});
        dataText.clear(); valueReadOnlyReason.clear(); newType=0; editableValue=true; addingData=true; editingData=true;
    }
    void DrawProfile(const std::vector<Entry>& entries,const ProcessManager& processes) {
        ImGui::InputText("슬롯 이름",&draft.name);
        const auto* entry=FindEntry(entries,draft.entryId);
        ImGui::Text("게임: %s",entry?entry->name.c_str():draft.entryId.empty()?"미배정":"등록 항목 없음");
        ImGui::BeginDisabled(draft.entryId.empty() || processes.Get(draft.entryId).alive);
        ImGui::InputText("게임 데이터 ID",&draft.instanceTypeId);
        ImGui::EndDisabled();
        ImGui::TextDisabled("같은 게임의 슬롯 데이터 구분에 사용합니다. 실행 중에는 변경할 수 없습니다.");
        ImGui::SeparatorText("영상과 오디오");
        const char* resolutions[]={"1920 × 1080 (HD)","1280 × 720","2560 × 1440","3840 × 2160","사용자 지정"};
        int resolution=draft.profile.width==1920&&draft.profile.height==1080?0:draft.profile.width==1280&&draft.profile.height==720?1:
            draft.profile.width==2560&&draft.profile.height==1440?2:draft.profile.width==3840&&draft.profile.height==2160?3:4;
        if(ImGui::Combo("해상도",&resolution,resolutions,5)) {
            if(resolution==0){draft.profile.width=1920;draft.profile.height=1080;}
            if(resolution==1){draft.profile.width=1280;draft.profile.height=720;}
            if(resolution==2){draft.profile.width=2560;draft.profile.height=1440;}
            if(resolution==3){draft.profile.width=3840;draft.profile.height=2160;draft.profile.latency=LiveLatency::Normal;}
        }
        ImGui::InputInt("너비",&draft.profile.width); ImGui::InputInt("높이",&draft.profile.height); ImGui::InputInt("FPS",&draft.profile.fps);
        int codec=static_cast<int>(draft.profile.codec); if(ImGui::Combo("비디오 코덱",&codec,"H.264\0HEVC (H.265)\0AV1\0"))draft.profile.codec=static_cast<VideoCodec>(codec);
        ImGui::InputInt("영상 비트레이트 (Kbps)",&draft.profile.videoBitrateKbps);
        ImGui::TextDisabled("CBR · 1080p60 H.264 기본값 17,000 Kbps · 게임 오디오 AAC");
        ImGui::InputInt("키프레임 간격 (초)",&draft.profile.keyframeSeconds);
        ImGui::InputText("인코더 (auto 또는 NVENC 이름)",&draft.profile.encoder);
        ImGui::TextWrapped("하드웨어 인코더를 사용할 수 없으면 새 슬롯의 송출 시작을 거부합니다. 기존 방송은 유지하며 CPU 전환이나 화질 자동 하향은 하지 않습니다.");
        ImGui::InputInt("AAC 비트레이트 (Kbps)",&draft.profile.audioBitrateKbps);
        int rate=draft.profile.audioSampleRate==44100?0:1;
        if(ImGui::Combo("오디오 샘플레이트",&rate,"44,100 Hz\0 48,000 Hz\0"))draft.profile.audioSampleRate=rate?48000:44100;
        ImGui::SeparatorText("생방송 지연과 연결");
        int latency=static_cast<int>(draft.profile.latency);
        if(ImGui::Combo("YouTube 생방송 지연",&latency,"일반\0짧게\0매우 짧게\0"))draft.profile.latency=static_cast<LiveLatency>(latency);
        ImGui::InputInt("추가 송출 지연 (초)",&draft.profile.streamDelaySeconds);
        ImGui::Checkbox("자동 재연결",&draft.profile.autoReconnect);
        ImGui::InputInt("재연결 최대 시도",&draft.profile.reconnectAttempts);
        ImGui::InputInt("첫 재시도 대기 (초)",&draft.profile.reconnectInitialSeconds);
        ImGui::Checkbox("송출 신호 중단 시 YouTube 방송 유지",&draft.profile.keepBroadcastOnSignalLoss);
        ImGui::TextWrapped("신호가 끊기면 새 영상은 전달되지 않습니다. YouTube 방송 자동 종료 설정과 재연결은 별개입니다.");
        ImGui::SeparatorText("일시정지 화면");
        ImGui::Checkbox("게임 일시정지 시 송출 중지",&draft.pause.stopOutput);
        ImGui::TextDisabled("기본값: 멈춘 게임 화면을 계속 송출");
        ImGui::InputText("오버레이 PNG 경로",&draft.pause.overlayPngPath);
        ImGui::SliderInt("사진 불투명도",&draft.pause.overlayOpacityPercent,0,100,"%d%%");
        ImGui::Checkbox("일시정지 중 게임 오디오 무음",&draft.pause.muteGameAudio);
        ImGui::SeparatorText("게임 교체 대기 화면");
        ImGui::InputText("대기 이미지 경로",&draft.waitingImagePath);
        ImGui::TextWrapped("게임 교체 중에는 대기 이미지와 무음을 송출하고, 새 게임 준비가 완료되면 전환합니다. 교체 전 유효한 이미지를 지정해야 합니다.");
        ImGui::SeparatorText("YouTube 연결");
        ImGui::InputText("방송 ID",&draft.connection.broadcastId);
        ImGui::InputText("채팅 ID",&draft.connection.liveChatId);
        ImGui::InputText("계정 자격 증명 참조",&draft.connection.accountRef);
        ImGui::InputText("송출 자격 증명 참조",&draft.connection.streamCredentialRef);
        ImGui::TextWrapped("실제 API 키나 송출 키를 입력하지 마세요. 보안 저장소 연결과 실제 송출 백엔드는 아직 연결되지 않았습니다.");
        if(ImGui::Button("방송 설정 저장"))Guard([&] {
            auto next=registry; next.Rename(managedId,draft.name); next.SetProfile(managedId,draft.profile); next.SetPause(managedId,draft.pause);
            next.SetConnection(managedId,draft.connection);
            next.SetWaitingImage(managedId,draft.waitingImagePath);
            if(next.Get(managedId).instanceTypeId!=draft.instanceTypeId)next.SetInstanceType(draft.entryId,draft.instanceTypeId);
            Save(std::move(next),true);
        });
    }
    void DrawData() {
        ImGui::TextWrapped("이 슬롯에 보관된 데이터를 직접 관리합니다. locked 항목도 운영자가 수정할 수 있습니다.");
        ImGui::InputText("게임 데이터 ID##filter",&dataFilter);
        if(ImGui::Checkbox("이 슬롯의 모든 게임 보기",&allGameData))Guard([&]{dataBuild=dataFilter;ReadData();});
        if(ImGui::Button("데이터 읽기"))Guard([&]{dataBuild=dataFilter;editingData=false;ReadData();});
        ImGui::SameLine(); ImGui::BeginDisabled(!allGameData && dataBuild.empty());
        if(ImGui::Button("추가"))AddData();
        ImGui::EndDisabled();
        ImGui::SameLine();ImGui::BeginDisabled(allGameData || dataBuild.empty());
        if(ImGui::Button("이 게임 초기화"))ImGui::OpenPopup("이 게임 데이터 초기화");
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("슬롯 전체 초기화"))ImGui::OpenPopup("슬롯 전체 데이터 초기화");
        ImGui::TextDisabled("현재 범위: %s · %zu개",allGameData?"이 슬롯의 모든 게임":dataBuild.empty()?"게임을 배정하세요":dataBuild.c_str(),records.size());
        if(ImGui::BeginPopupModal("이 게임 데이터 초기화",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("현재 슬롯의 게임 '%s' 데이터만 초기화합니다. 다른 게임 데이터는 유지됩니다. 등록된 변수는 다음 사용 시 기본값을 사용합니다.",dataBuild.c_str());
            if(ImGui::Button("이 게임 초기화##confirm")) {
                Guard([&] { const auto db=store; const auto slot=managedId,build=dataBuild;
                    Launch(Operation::WriteData,[db,slot,build]{db->ResetSlot("local",slot,build);Result result;result.records=db->ListSlot("local",slot,build);return result;}); });
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();if(ImGui::Button("취소"))ImGui::CloseCurrentPopup(); ImGui::EndPopup();
        }
        if(ImGui::BeginPopupModal("슬롯 전체 데이터 초기화",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("이 슬롯에 저장된 모든 게임 데이터를 초기화합니다. 다른 슬롯과 global 데이터는 유지됩니다. 등록된 변수는 다음 사용 시 기본값을 사용합니다.");
            if(ImGui::Button("슬롯 전체 초기화##confirm")) {
                Guard([&] { const auto db=store;const auto slot=managedId,build=dataBuild;const bool all=allGameData;
                    Launch(Operation::WriteData,[db,slot,build,all]{db->ResetSlotAll("local",slot);Result result;result.records=all?db->ListSlotAll("local",slot):db->ListSlot("local",slot,build);return result;}); });
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();if(ImGui::Button("취소"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
        if(ImGui::BeginTable("slot-data",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("이름");ImGui::TableSetupColumn("설명");ImGui::TableSetupColumn("값 / 형식");
            ImGui::TableSetupColumn("편집",ImGuiTableColumnFlags_WidthFixed,60);ImGui::TableSetupColumn("삭제",ImGuiTableColumnFlags_WidthFixed,60);ImGui::TableHeadersRow();
            for(const auto& row:records) {
                ImGui::PushID(row.key.Encode().c_str());ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s",row.name.c_str());ImGui::TextDisabled("%s",row.key.dataId.c_str());
                if(allGameData)ImGui::TextDisabled("게임: %s",row.key.build.c_str());
                ImGui::TableSetColumnIndex(1);ImGui::TextWrapped("%s",row.description.c_str());
                ImGui::TableSetColumnIndex(2);
                auto& preview=valuePreviews[row.key.Encode()];
                if(preview.first!=row.revision) {
                    try { auto value=EditText(row.value);preview.second=value?value->substr(0,160):"값 스키마 없음 (읽기 전용)"; }
                    catch(const std::exception& failure){preview.second=std::string("값 표시 불가: ")+failure.what();}
                    preview.first=row.revision;
                }
                ImGui::TextWrapped("%s",preview.second.c_str());
                ImGui::TextDisabled("%s",row.value.type.c_str());ImGui::TableSetColumnIndex(3);
                if(ImGui::SmallButton("편집"))Guard([&]{EditData(row);});ImGui::TableSetColumnIndex(4);
                if(ImGui::SmallButton("삭제"))Guard([&]{WriteData(row,true);});ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if(editingData) {
            ImGui::SeparatorText(addingData?"항목 추가":"항목 편집");
            ImGui::BeginDisabled(!addingData);ImGui::InputText("데이터 ID",&dataDraft.key.dataId);ImGui::EndDisabled();
            ImGui::BeginDisabled(!addingData || !allGameData);ImGui::InputText("항목의 게임 데이터 ID",&dataDraft.key.build);ImGui::EndDisabled();
            ImGui::InputText("데이터 이름",&dataDraft.name);ImGui::InputText("데이터 설명",&dataDraft.description);
            if(addingData && ImGui::Combo("형식",&newType,Types,static_cast<int>(std::size(Types)))) {
                dataDraft.value.type=Types[newType];
                dataText=newType==0?"":newType==3?"false":newType==8?"[0,0]":newType==9||newType==14?"[0,0,0]":newType==10?"[0,0,0,0]":newType>=11?"[]":"0";
            }
            if(editableValue) {
                if(dataDraft.value.type=="bool") { bool value=dataText=="true";if(ImGui::Checkbox("값",&value))dataText=value?"true":"false"; }
                else ImGui::InputTextMultiline("데이터 값",&dataText,{0,90});
                if(GenericDataEditor::UsesJson(dataDraft.value.type,dataDraft.value.codecVersion))
                    ImGui::TextWrapped("벡터·배열·목록은 JSON 배열로, 등록된 구조체는 JSON 객체로 입력하세요. 예: [1.5, 2, 3]");
            } else ImGui::TextWrapped("%s",valueReadOnlyReason.c_str());
            if(ImGui::Button(addingData?"항목 추가 저장":"항목 변경 저장"))Guard([&] {
                if(dataDraft.name.empty())throw std::runtime_error("데이터 이름을 입력하세요.");
                if(dataDraft.key.build.empty())throw std::runtime_error("항목의 게임 데이터 ID를 입력하세요.");
                auto value=dataDraft;if(editableValue)value.value=EncodeText(value.value.type,value.value.codecVersion,dataText);WriteData(std::move(value));
            });
            ImGui::SameLine();if(ImGui::Button("편집 취소"))editingData=false;
        }
    }
};
StreamingPanel::StreamingPanel(const std::filesystem::path& database):impl_(std::make_unique<Impl>(database)) {}
StreamingPanel::~StreamingPanel()=default;
bool StreamingPanel::Busy()const{return impl_->Busy();}
bool StreamingPanel::Ready()const{return impl_->ready && impl_->healthy;}
void StreamingPanel::OpenManagement(const std::string& slotId) {
    if(!Ready() || Busy())throw std::runtime_error("슬롯 데이터를 준비하는 중입니다.");
    impl_->Manage(slotId);
}
bool StreamingPanel::HasSlotForEntry(const std::string& entryId)const {
    return std::any_of(impl_->registry.Slots().begin(),impl_->registry.Slots().end(),[&](const Slot& slot){return slot.entryId==entryId;});
}
void StreamingPanel::Tick(const std::vector<Entry>& entries,const ProcessManager& processes) {
    auto& p=*impl_;p.Poll();if(!p.ready || p.Busy())return;
    std::set<std::string> configured;
    for(const auto& slot:p.registry.Slots())if(!slot.entryId.empty())configured.insert(slot.entryId);
    for(const auto& id:configured)p.Guard([&] {
        const auto status=processes.Get(id);
        if(FindEntry(entries,id) && status.alive && !status.runId.empty())p.registry.BindEntry(id,status.runId);
        else p.registry.UnbindEntry(id);
    });
}
void StreamingPanel::Draw(const std::vector<Entry>& entries,ProcessManager& processes,bool stopping) {
    auto& p=*impl_;
    ImGui::TextUnformatted("방송 슬롯");
    ImGui::TextWrapped("한 슬롯에 한 게임을 배정합니다. 여러 슬롯에 같은 빌드 항목을 배정하면 실행 인스턴스를 공유합니다.");
    ImGui::TextColored({1,.75f,.40f,1},"실제 YouTube 송출 연결은 아직 구현되지 않았습니다. 현재는 슬롯·방송 설정·데이터를 저장할 수 있습니다.");
    if(!p.error.empty())ImGui::TextWrapped("%s",p.error.c_str());
    if(!p.info.empty())ImGui::TextDisabled("%s",p.info.c_str());
    if(p.Busy())ImGui::TextDisabled("데이터 처리 중…");
    ImGui::BeginDisabled(p.Busy() || stopping);
    if(ImGui::Button("다시 읽기"))p.Guard([&]{p.Load();});
    ImGui::SameLine();ImGui::BeginDisabled(!p.ready || !p.healthy);
    if(ImGui::Button("+ 방송 슬롯 추가"))p.Guard([&] {
        auto next=p.registry;Slot slot;slot.slotId=NewId();slot.name="방송 "+std::to_string(next.Slots().size()+1);next.Add(std::move(slot));p.Save(std::move(next));
    });
    ImGui::EndDisabled();ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::BeginDisabled(p.Busy() || !p.ready || !p.healthy || stopping);
    if(ImGui::BeginTable("stream-slots",6,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollY|ImGuiTableFlags_SizingStretchProp,{0,-32})) {
        ImGui::TableSetupColumn("슬롯 / 출력");ImGui::TableSetupColumn("게임 인스턴스",ImGuiTableColumnFlags_WidthStretch,1.5f);
        ImGui::TableSetupColumn("메인",ImGuiTableColumnFlags_WidthFixed,70);ImGui::TableSetupColumn("실행 / 송출",ImGuiTableColumnFlags_WidthFixed,180);
        ImGui::TableSetupColumn("관리",ImGuiTableColumnFlags_WidthFixed,70);ImGui::TableSetupColumn("삭제",ImGuiTableColumnFlags_WidthFixed,70);
        ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
        for(const auto& slot:p.registry.Slots()) {
            ImGui::PushID(slot.slotId.c_str());ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(slot.name.c_str());ImGui::TextDisabled("%d×%d %dfps · %s",slot.profile.width,slot.profile.height,slot.profile.fps,CodecName(slot.profile.codec));
            ImGui::TableSetColumnIndex(1);const auto* entry=FindEntry(entries,slot.entryId);
            if(ImGui::BeginCombo("##entry",entry?entry->name.c_str():slot.entryId.empty()?"게임 선택":"등록 항목 없음")) {
                if(ImGui::Selectable("배정 해제",slot.entryId.empty()))p.Guard([&]{auto next=p.registry;next.ClearAssignment(slot.slotId);p.Save(std::move(next));});
                for(const auto& item:entries)if(ImGui::Selectable((item.name+"##"+item.id).c_str(),item.id==slot.entryId))p.Guard([&] {
                    auto next=p.registry;std::string type="entry:"+item.id;
                    for(const auto& candidate:next.Slots())if(candidate.entryId==item.id){type=candidate.instanceTypeId;break;}
                    next.ConfigureInstance(slot.slotId,item.id,type);p.Save(std::move(next));
                });
                ImGui::EndCombo();
            }
            const auto status=processes.Get(slot.entryId);
            if(status.alive)ImGui::TextDisabled("공유 인스턴스 PID %lu",status.pid);else ImGui::TextDisabled("인스턴스 정지");
            ImGui::TableSetColumnIndex(2);ImGui::BeginDisabled(slot.entryId.empty());
            if(ImGui::RadioButton("메인",slot.isMain))p.Guard([&]{auto next=p.registry;next.SetMain(slot.slotId);p.Save(std::move(next));});ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(3);ImGui::BeginDisabled(!entry || status.alive);
            if(ImGui::SmallButton("게임 실행") && entry)p.Guard([&]{processes.Start(*entry,SupportsControl(entry->executable)?entry->windowMode:"windowed");});ImGui::EndDisabled();
            ImGui::SameLine();ImGui::BeginDisabled();ImGui::SmallButton("송출 시작");ImGui::EndDisabled();
            ImGui::TextDisabled("송출 미연결");ImGui::TableSetColumnIndex(4);
            if(ImGui::SmallButton("관리"))p.Guard([&]{p.Manage(slot.slotId);});ImGui::TableSetColumnIndex(5);
            if(ImGui::SmallButton("삭제")){p.deleteSlotId=slot.slotId;ImGui::OpenPopup("슬롯 삭제");}
            if(ImGui::BeginPopupModal("슬롯 삭제",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("슬롯 설정을 삭제할까요? 저장된 게임 데이터는 유지됩니다. 데이터를 지우려면 먼저 데이터 관리에서 초기화하세요.");
                if(ImGui::Button("삭제##confirm")) {
                    p.Guard([&]{auto next=p.registry;next.Remove(p.deleteSlotId);p.Save(std::move(next));if(p.managedId==p.deleteSlotId)p.managementOpen=false;});ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();if(ImGui::Button("취소"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
}
void StreamingPanel::DrawWindows(const std::vector<Entry>& entries,const ProcessManager& processes) {
    auto& p=*impl_;if(!p.managementOpen)return;
    ImGui::SetNextWindowSize({760,650},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("슬롯 관리###streaming-management",&p.managementOpen)) {
        if(!p.error.empty())ImGui::TextWrapped("%s",p.error.c_str());
        if(p.Busy())ImGui::TextDisabled("데이터 처리 중…");
        ImGui::TextDisabled("%s",p.managedId.c_str());
        ImGui::BeginDisabled(p.Busy());
        if(ImGui::BeginTabBar("slot-management-tabs")) {
            if(ImGui::BeginTabItem("방송 설정")){p.DrawProfile(entries,processes);ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("데이터 관리")){p.DrawData();ImGui::EndTabItem();}
            ImGui::EndTabBar();
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}
}
