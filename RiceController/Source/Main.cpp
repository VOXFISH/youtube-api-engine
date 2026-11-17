#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "ControllerModel.h"
#include "ProcessManager.h"
#include "Streaming/StreamingPanel.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace RC = RiceController;
namespace fs = std::filesystem;
namespace {
enum class Icon { Play, Pause, Stop, Broadcast, Wrench, File, Maximize, Minimize, Close };

bool IconButton(const char* id, Icon icon, const char* hint, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Button(id, {34, 32});
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 c{p.x + 17, p.y + 16};
    ImVec4 color = icon == Icon::Play ? ImVec4{.35f, .90f, .61f, 1} :
                   icon == Icon::Broadcast ? ImVec4{1, .37f, .40f, 1} : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (!enabled) color.w *= .35f;
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(color);
    switch (icon) {
    case Icon::Play: draw->AddTriangleFilled({c.x-5,c.y-7},{c.x-5,c.y+7},{c.x+7,c.y},col); break;
    case Icon::Pause:
        draw->AddRectFilled({c.x-6,c.y-7},{c.x-2,c.y+7},col,1);
        draw->AddRectFilled({c.x+2,c.y-7},{c.x+6,c.y+7},col,1); break;
    case Icon::Stop: draw->AddRectFilled({c.x-6,c.y-6},{c.x+6,c.y+6},col,1); break;
    case Icon::Broadcast: draw->AddCircleFilled(c,7,col,20); break;
    case Icon::Maximize: draw->AddRect({c.x-7,c.y-6},{c.x+7,c.y+6},col,1,0,1.7f); break;
    case Icon::Minimize: draw->AddLine({c.x-7,c.y},{c.x+7,c.y},col,2); break;
    case Icon::Close:
        draw->AddLine({c.x-5,c.y-5},{c.x+5,c.y+5},col,1.8f);
        draw->AddLine({c.x-5,c.y+5},{c.x+5,c.y-5},col,1.8f); break;
    case Icon::File:
        draw->AddPolyline(std::array<ImVec2,6>{{{c.x-6,c.y-8},{c.x+2,c.y-8},{c.x+7,c.y-3},{c.x+7,c.y+8},{c.x-6,c.y+8},{c.x-6,c.y-8}}}.data(),6,col,0,1.5f);
        draw->AddLine({c.x+2,c.y-8},{c.x+2,c.y-3},col,1.5f);
        draw->AddLine({c.x+2,c.y-3},{c.x+7,c.y-3},col,1.5f); break;
    case Icon::Wrench:
        draw->AddLine({c.x-6,c.y+7},{c.x+4,c.y-3},col,4);
        draw->AddCircle({c.x+4,c.y-4},5,col,18,2);
        draw->AddTriangleFilled({c.x+3,c.y-10},{c.x+10,c.y-3},{c.x+3,c.y-3},ImGui::GetColorU32(ImGuiCol_Button));
        draw->AddCircle({c.x-6,c.y+7},2,col,12,1); break;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",hint);
    return pressed;
}

std::optional<fs::path> PickPath(bool folder, bool project) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))
        throw std::runtime_error("파일 선택기를 열 수 없습니다.");
    DWORD flags = 0; dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    COMDLG_FILTERSPEC filter{project ? L"Rice 프로젝트" : L"Rice 플레이어",project ? L"project.rice" : L"*.exe"};
    if (!folder) dialog->SetFileTypes(1,&filter);
    dialog->SetTitle(project ? L"프로젝트 선택" : L"빌드 실행 파일 선택");
    const HRESULT shown = dialog->Show(GetActiveWindow());
    std::optional<fs::path> result;
    if (SUCCEEDED(shown)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) { result = path; CoTaskMemFree(path); }
            item->Release();
        }
    }
    dialog->Release();
    if (FAILED(shown) && shown != HRESULT_FROM_WIN32(ERROR_CANCELLED)) throw std::runtime_error("파일 선택에 실패했습니다.");
    return result;
}

void RevealFile(const fs::path& path) {
    if (!fs::is_regular_file(path)) throw std::runtime_error("실행 파일이 없습니다. 경로 변경으로 위치를 수정하세요.");
    PIDLIST_ABSOLUTE item = nullptr;
    HRESULT result = SHParseDisplayName(fs::absolute(path).c_str(),nullptr,&item,0,nullptr);
    if (SUCCEEDED(result)) { result = SHOpenFolderAndSelectItems(item,0,nullptr,0); CoTaskMemFree(item); }
    if (FAILED(result)) throw std::runtime_error("파일 탐색기를 열 수 없습니다.");
}

const char* StateText(const RC::Status& s) {
    switch (s.state) {
    case RC::RunState::Starting: return "시작 중";
    case RC::RunState::Running: return s.controlled ? "실행 중" : "실행 중 (제한 모드)";
    case RC::RunState::Paused: return "일시정지";
    case RC::RunState::Stopping: return "종료 중";
    case RC::RunState::Error: return "오류";
    default: return "정지";
    }
}
const char* ModeText(const std::string& mode) { return mode == "hidden" ? "숨김" : mode == "fullscreen" ? "전체화면" : "창 모드"; }

struct SettingsWindow {
    bool open = false, minimized = false, maximized = false, focus = false, restore = false;
    ImVec2 position{140,160},size{540,360};
};

class ControllerUI {
    GLFWwindow* window;
    fs::path config;
    std::vector<RC::Entry> entries;
    RC::ProcessManager processes;
    std::unique_ptr<RC::Streaming::StreamingPanel> streaming;
    bool selectStreaming = false;
    std::map<std::string,SettingsWindow> settings;
    std::string notice, editError, removeId, forceId;
    std::set<std::string> pendingRemovals;
    bool configHealthy = true, editRequested = false, removeRequested = false, forceRequested = false;
    bool exitRequested = false, stopForExit = false, quit = false;
    int editKind = 0; // 0 add, 1 rename, 2 paths
    RC::Entry draft;
    std::string draftExe, draftProject;
    std::array<bool,3> globalOpen{};

    template<class F> void Guard(F&& action) { try { action(); } catch(const std::exception& e) { notice=e.what(); } }
    RC::Entry* Find(const std::string& id) { auto i=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.id==id;});return i==entries.end()?nullptr:&*i; }
    void Commit(std::vector<RC::Entry> next) {
        if (!configHealthy) throw std::runtime_error("손상된 설정 파일을 보존했습니다. --config로 다른 파일을 선택하거나 원본을 복구하세요.");
        RC::SaveEntries(config,next); entries=std::move(next);
    }
    void Remove(const std::string& id) {
        auto next=entries;std::erase_if(next,[&](const auto& e){return e.id==id;});Commit(std::move(next));settings.erase(id);
    }
    void Edit(const RC::Entry* entry,int kind) {
        editKind=kind;draft=entry?*entry:RC::Entry{RC::NewId(),RC::DefaultName(entries),{}, {},"windowed"};
        draftExe=RC::PathText(draft.executable);draftProject=RC::PathText(draft.project);editError.clear();editRequested=true;
    }
    void SetMode(RC::Entry& entry,const std::string& mode) {
        if (processes.Get(entry.id).alive) processes.SetWindowMode(entry.id,mode); else processes.Start(entry,mode);
        // Save the requested next-start mode; actual runtime mode comes from acknowledgements.
        auto next=entries;for(auto& e:next)if(e.id==entry.id)e.windowMode=mode;
        RC::SaveEntries(config,next);entry.windowMode=mode;
    }
    void Row(RC::Entry& entry) {
        const auto s=processes.Get(entry.id);
        const bool controlled=s.alive?s.controlled:RC::SupportsControl(entry.executable);
        const bool busy=s.pending||s.state==RC::RunState::Starting||s.state==RC::RunState::Stopping;
        const bool ready=s.state==RC::RunState::Running||s.state==RC::RunState::Paused;
        std::error_code pathError;
        const auto project=fs::is_directory(entry.project,pathError)?entry.project/"project.rice":entry.project;
        const bool pathValid=fs::is_regular_file(entry.executable,pathError)&&fs::is_regular_file(project,pathError);
        ImGui::PushID(entry.id.c_str());
        ImGui::TableNextRow(ImGuiTableRowFlags_None,70);
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::Selectable("##name",false,ImGuiSelectableFlags_AllowDoubleClick,{0,25});
        ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(),ImGui::GetColorU32(ImGuiCol_Text),entry.name.c_str());
        if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0))Edit(&entry,1);
        if(ImGui::BeginPopupContextItem("항목 메뉴")){
            if(ImGui::MenuItem("이름 변경",nullptr,false,configHealthy))Edit(&entry,1);
            if(ImGui::MenuItem("경로 변경",nullptr,false,!s.alive&&configHealthy))Edit(&entry,2);
            ImGui::Separator();
            if(ImGui::MenuItem("항목 제거",nullptr,false,configHealthy)){removeId=entry.id;removeRequested=true;}
            ImGui::EndPopup();
        }
        auto label=RC::PathText(entry.executable.parent_path());
        ImGui::TextDisabled("%s",label.c_str());
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",RC::PathText(entry.executable).c_str(),RC::PathText(entry.project).c_str());
        ImGui::TableSetColumnIndex(1);
        const auto color=s.state==RC::RunState::Error||!pathValid?ImVec4{1,.49f,.45f,1}:s.state==RC::RunState::Running?ImVec4{.4f,.88f,.62f,1}:ImVec4{.72f,.76f,.85f,1};
        ImGui::AlignTextToFramePadding();ImGui::TextColored(color,"%s",!pathValid&&!s.alive?"경로 오류":StateText(s));
        if(s.alive)ImGui::TextDisabled("PID %lu · %s",s.pid,ModeText(s.windowMode));
        else ImGui::TextDisabled("%s",ModeText(controlled?entry.windowMode:"windowed"));
        if(!s.error.empty()&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",s.error.c_str());
        if(s.state==RC::RunState::Error){
            if(ImGui::SmallButton("오류 보기"))notice=s.error;
            if(s.alive){
                ImGui::SameLine();if(ImGui::SmallButton("강제 종료")){forceId=entry.id;forceRequested=true;}
                if(ImGui::SmallButton("계속 대기"))Guard([&]{processes.ContinueWaiting(entry.id);});
            }
        }
        ImGui::TableSetColumnIndex(2);
        if(IconButton("##play",Icon::Play,s.state==RC::RunState::Paused?"재개":"재생",!stopForExit&&!busy&&pathValid&&(!s.alive||s.state==RC::RunState::Paused)))Guard([&]{if(s.alive)processes.Resume(entry.id);else processes.Start(entry,controlled?entry.windowMode:"windowed");});
        ImGui::TableSetColumnIndex(3);
        if(IconButton("##pause",Icon::Pause,controlled?"일시정지":"이 빌드는 외부 일시정지를 지원하지 않습니다",controlled&&!busy&&s.state==RC::RunState::Running))Guard([&]{processes.Pause(entry.id);});
        ImGui::TableSetColumnIndex(4);
        if(IconButton("##stop",Icon::Stop,"정지 / 시작 취소",s.alive&&s.state!=RC::RunState::Stopping))Guard([&]{processes.Stop(entry.id);});
        ImGui::TableSetColumnIndex(5);
        if(IconButton("##broadcast",Icon::Broadcast,"방송 슬롯 설정 열기"))selectStreaming=true;
        ImGui::TableSetColumnIndex(6);
        if(IconButton("##settings",Icon::Wrench,"설정")){auto& panel=settings[entry.id];panel.open=true;if(panel.minimized)panel.restore=true;panel.minimized=false;panel.focus=true;}
        ImGui::TableSetColumnIndex(7);
        if(IconButton("##location",Icon::File,"파일 탐색기에서 위치 열기"))Guard([&]{RevealFile(entry.executable);});
        ImGui::TableSetColumnIndex(8);
        if(IconButton("##maximize",Icon::Maximize,controlled?"전체화면으로 실행 / 표시":"이 빌드는 전체화면 제어를 지원하지 않습니다",!stopForExit&&controlled&&!busy&&pathValid&&(!s.alive||ready)))Guard([&]{SetMode(entry,"fullscreen");});
        ImGui::TableSetColumnIndex(9);
        if(IconButton("##minimize",Icon::Minimize,controlled?"화면을 숨긴 채 실행":"이 빌드는 숨김 실행을 지원하지 않습니다",!stopForExit&&controlled&&!busy&&pathValid&&(!s.alive||ready)))Guard([&]{SetMode(entry,"hidden");});
        ImGui::TableSetColumnIndex(10);
        if(IconButton("##remove",Icon::Close,"항목 제거",configHealthy)){removeId=entry.id;removeRequested=true;}
        ImGui::PopID();
    }
    void EditDialog() {
        if(editRequested){ImGui::OpenPopup("빌드 항목###edit");editRequested=false;}
        ImGui::SetNextWindowSize({660,0},ImGuiCond_Appearing);
        bool open=true;
        if(ImGui::BeginPopupModal("빌드 항목###edit",&open,ImGuiWindowFlags_AlwaysAutoResize)){
            if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);ImGui::BeginDisabled(editKind==2);const bool submitted=ImGui::InputText("##name",&draft.name,ImGuiInputTextFlags_EnterReturnsTrue);ImGui::EndDisabled();
            ImGui::TextDisabled("이름 · 1~64자");
            if(editKind!=1){
                ImGui::Spacing();ImGui::TextUnformatted("실행 파일 또는 빌드 폴더");
                ImGui::SetNextItemWidth(-1);ImGui::InputText("##exe",&draftExe);
                if(ImGui::Button("실행 파일 선택"))Guard([&]{if(auto p=PickPath(false,false)){draftExe=RC::PathText(*p);auto project=p->parent_path()/"Project";if(fs::is_regular_file(project/"project.rice"))draftProject=RC::PathText(project);}});
                ImGui::SameLine();if(ImGui::Button("빌드 폴더 선택"))Guard([&]{if(auto p=PickPath(true,false)){draftExe=RC::PathText(*p/"RicePlayer.exe");if(fs::is_regular_file(*p/"Project/project.rice"))draftProject=RC::PathText(*p/"Project");}});
                ImGui::Spacing();ImGui::TextUnformatted("프로젝트 폴더 또는 project.rice");
                ImGui::SetNextItemWidth(-1);ImGui::InputText("##project",&draftProject);
                if(ImGui::Button("프로젝트 파일 선택"))Guard([&]{if(auto p=PickPath(false,true))draftProject=RC::PathText(*p);});
                ImGui::SameLine();if(ImGui::Button("프로젝트 폴더 선택"))Guard([&]{if(auto p=PickPath(true,true))draftProject=RC::PathText(*p);});
            }
            if(!editError.empty())ImGui::TextWrapped("%s",editError.c_str());
            ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
            const bool enter=ImGui::IsKeyPressed(ImGuiKey_Enter)&&!ImGui::IsAnyItemActive();
            if(ImGui::Button(editKind==0?"추가":"확정",{100,32})||enter||submitted){
                try{
                    auto candidate=draft;
                    if(editKind==1)RC::ValidateName(candidate.name);
                    else{candidate.executable=RC::TextPath(draftExe);candidate.project=RC::TextPath(draftProject);RC::ValidateEntry(candidate);}
                    auto next=entries;
                    if(editKind==0)next.push_back(candidate);else for(auto& e:next)if(e.id==candidate.id)e=candidate;
                    Commit(std::move(next));ImGui::CloseCurrentPopup();
                }catch(const std::exception& e){editError=e.what();}
            }
            ImGui::SameLine();if(ImGui::Button("취소",{100,32})||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    void ConfirmDialogs() {
        if(removeRequested){ImGui::OpenPopup("항목 제거");removeRequested=false;}
        if(ImGui::BeginPopupModal("항목 제거",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            auto* e=Find(removeId);const bool alive=processes.Get(removeId).alive;
            ImGui::Text("%s 항목을 제거할까요?",e?e->name.c_str():"");ImGui::TextUnformatted("빌드 파일과 프로젝트는 그대로 유지됩니다.");
            if(ImGui::Button(alive?"중지 후 제거":"제거")){
                Guard([&]{if(alive){processes.Stop(removeId);pendingRemovals.insert(removeId);}else Remove(removeId);});ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();if(ImGui::Button("취소")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
        if(forceRequested){ImGui::OpenPopup("강제 종료");forceRequested=false;}
        if(ImGui::BeginPopupModal("강제 종료",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextUnformatted("응답하지 않는 이 인스턴스를 강제 종료할까요?");
            if(ImGui::Button("강제 종료")){Guard([&]{processes.ForceStop(forceId);});ImGui::CloseCurrentPopup();}
            ImGui::SameLine();if(ImGui::Button("취소")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
        if(exitRequested){ImGui::OpenPopup("컨트롤러 종료");exitRequested=false;}
        if(ImGui::BeginPopupModal("컨트롤러 종료",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextUnformatted("모든 방송이 종료됩니다. Rice Controller를 종료하시겠습니까?");
            ImGui::BeginDisabled(streaming&&streaming->Busy());
            if(ImGui::Button("종료")){
                for(const auto& e:entries)if(processes.Get(e.id).alive)Guard([&]{processes.Stop(e.id);});
                stopForExit=true;ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();if(ImGui::Button("취소")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
    }
    void SettingsPanels() {
        for(auto& [id,p]:settings){
            auto* entry=Find(id);if(!entry||!p.open)continue;
            const std::string title=entry->name+" 설정###settings-"+id;
            auto* viewport=ImGui::GetMainViewport();
            if(p.maximized){ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(viewport->WorkSize);}
            else if(p.restore){ImGui::SetNextWindowPos(p.position);ImGui::SetNextWindowSize(p.size);p.restore=false;}
            else ImGui::SetNextWindowSize(p.size,ImGuiCond_FirstUseEver);
            if(p.minimized)ImGui::SetNextWindowSize({p.size.x,72});
            if(p.focus){ImGui::SetNextWindowFocus();p.focus=false;}
            ImGui::SetNextWindowSizeConstraints({300,72},{10000,10000});
            ImGui::Begin(title.c_str(),nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|(p.minimized||p.maximized?ImGuiWindowFlags_NoResize:0));
            if(!p.minimized&&!p.maximized){p.position=ImGui::GetWindowPos();p.size=ImGui::GetWindowSize();}
            ImGui::AlignTextToFramePadding();ImGui::TextUnformatted((entry->name+" 설정").c_str());
            ImGui::SameLine(std::max(155.f,ImGui::GetWindowWidth()-128));
            if(IconButton("##panelmax",Icon::Maximize,p.maximized?"복원":"최대화")){p.maximized=!p.maximized;p.minimized=false;if(!p.maximized)p.restore=true;}
            ImGui::SameLine();if(IconButton("##panelmin",Icon::Minimize,p.minimized?"복원":"최소화")){if(p.maximized)p.maximized=false;p.minimized=!p.minimized;if(!p.minimized)p.restore=true;}
            ImGui::SameLine();if(IconButton("##panelclose",Icon::Close,"닫기"))p.open=false;
            ImGui::End();
        }
        const char* names[]={"기본 빌드 폴더 지정","방송 설정","저장소 설정"};
        for(int i=0;i<3;++i)if(globalOpen[i]){
            ImGui::SetNextWindowSize({440,240},ImGuiCond_FirstUseEver);
            if(ImGui::Begin(names[i],&globalOpen[i]))ImGui::TextDisabled("준비 중");ImGui::End();
        }
    }
public:
    ControllerUI(GLFWwindow* native,fs::path file):window(native),config(std::move(file)) {
        try{entries=RC::LoadEntries(config);}catch(const std::exception& e){configHealthy=false;notice=std::string("설정을 읽지 못했습니다. 원본을 보존합니다: ")+e.what();}
        auto database=config;database.replace_extension(".streaming.sqlite3");
        streaming=std::make_unique<RC::Streaming::StreamingPanel>(database);
    }
    bool Done()const{return quit;}
    void Tick() {
        processes.Poll();
        streaming->Tick(entries,processes);
        for(auto it=pendingRemovals.begin();it!=pendingRemovals.end();){
            const auto status=processes.Get(*it);
            if(!status.alive&&status.state==RC::RunState::Stopped){auto id=*it;it=pendingRemovals.erase(it);Guard([&]{Remove(id);});}
            else if(status.state==RC::RunState::Error){notice="정상 종료를 확인하지 못해 항목을 유지했습니다. "+status.error;it=pendingRemovals.erase(it);}
            else ++it;
        }
        if(stopForExit&&!processes.AnyAlive()&&!streaming->Busy())quit=true;
        if(glfwWindowShouldClose(window)){
            glfwSetWindowShouldClose(window,GLFW_FALSE);
            exitRequested=true;
        }
    }
    void Draw() {
        const auto* vp=ImGui::GetMainViewport();ImGui::SetNextWindowPos(vp->WorkPos);ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("Rice Controller",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_MenuBar);
        if(ImGui::BeginMenuBar()){
            if(ImGui::BeginMenu("전체 설정")){
                if(ImGui::MenuItem("기본 빌드 폴더 지정"))globalOpen[0]=true;
                if(ImGui::MenuItem("방송 설정"))selectStreaming=true;
                if(ImGui::MenuItem("저장소 설정"))globalOpen[2]=true;ImGui::EndMenu();
            }ImGui::EndMenuBar();
        }
        ImGui::Spacing();ImGui::TextUnformatted("RICE CONTROLLER");ImGui::Spacing();
        ImGui::TextDisabled("빌드 인스턴스를 독립적으로 실행하고 관리합니다.");
        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
        if(ImGui::BeginTabBar("controller-sections")){
        if(ImGui::BeginTabItem("빌드")){
        ImGui::BeginDisabled(!configHealthy||stopForExit);if(ImGui::Button("+ 항목 추가",{142,36}))Edit(nullptr,0);ImGui::EndDisabled();
        ImGui::SameLine();ImGui::TextDisabled("%zu개 항목 · 이름을 두 번 클릭하여 편집",entries.size());
        if(stopForExit){ImGui::TextUnformatted("모든 빌드가 종료되면 컨트롤러를 닫습니다.");ImGui::SameLine();if(ImGui::SmallButton("종료 대기 취소"))stopForExit=false;}
        if(!notice.empty()){
            ImGui::Spacing();ImGui::PushStyleColor(ImGuiCol_Text,{1,.72f,.42f,1});ImGui::TextWrapped("%s",notice.c_str());ImGui::PopStyleColor();
            if(ImGui::SmallButton("안내 닫기"))notice.clear();
        }
        ImGui::Spacing();
        if(ImGui::BeginTable("builds",11,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollY|ImGuiTableFlags_SizingStretchProp,{0,-32})){
            ImGui::TableSetupColumn("이름 / 빌드 위치",ImGuiTableColumnFlags_WidthStretch,1.3f);
            ImGui::TableSetupColumn("상태",ImGuiTableColumnFlags_WidthFixed,208);
            for(const char* heading:{"재생","일시정지","정지","송출","설정","위치","최대화","최소화","제거"})ImGui::TableSetupColumn(heading,ImGuiTableColumnFlags_WidthFixed,53);
            ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
            for(size_t i=0;i<entries.size();++i)Row(entries[i]);
            if(entries.empty()){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextDisabled("+ 항목 추가로 첫 빌드를 등록하세요.");}
            ImGui::EndTable();
        }
        ImGui::TextDisabled("방송 슬롯에서 출력 설정과 슬롯 데이터를 관리할 수 있습니다.");
        ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("방송 슬롯",nullptr,selectStreaming?ImGuiTabItemFlags_SetSelected:0)){
            selectStreaming=false;streaming->Draw(entries,processes,stopForExit);ImGui::EndTabItem();
        }
        ImGui::EndTabBar();}
        EditDialog();ConfirmDialogs();ImGui::End();SettingsPanels();streaming->DrawWindows(entries,processes);
    }
};

void Style() {
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();
    s.WindowPadding={18,16};s.FramePadding={10,7};s.ItemSpacing={8,8};s.CellPadding={10,10};
    s.WindowRounding=8;s.FrameRounding=5;s.PopupRounding=7;s.WindowBorderSize=1;
    s.Colors[ImGuiCol_WindowBg]={.075f,.087f,.115f,1};s.Colors[ImGuiCol_PopupBg]={.11f,.125f,.16f,1};
    s.Colors[ImGuiCol_Button]={.16f,.19f,.24f,1};s.Colors[ImGuiCol_ButtonHovered]={.23f,.30f,.37f,1};
    s.Colors[ImGuiCol_ButtonActive]={.25f,.38f,.43f,1};s.Colors[ImGuiCol_Header]={.16f,.23f,.28f,1};
    s.Colors[ImGuiCol_HeaderHovered]={.20f,.30f,.34f,1};s.Colors[ImGuiCol_TableHeaderBg]={.11f,.14f,.18f,1};
    s.Colors[ImGuiCol_Text]={.91f,.94f,.98f,1};s.Colors[ImGuiCol_TextDisabled]={.53f,.60f,.68f,1};
}
}

int main() {
    bool smoke=false;GLFWwindow* window=nullptr;bool gui=false,platform=false,renderer=false;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;
    try {
        fs::path config=RC::DefaultConfigPath();
        int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        if(!argv)throw std::runtime_error("Cannot read command line");
        std::vector<std::wstring> args(argv,argv+argc);LocalFree(argv);
        for(size_t i=1;i<args.size();++i){
            if(args[i]==L"--smoke")smoke=true;
            else if(args[i]==L"--config"&&i+1<args.size())config=fs::absolute(args[++i]);
            else throw std::runtime_error("Usage: RiceController.exe [--config file] [--smoke]");
        }
        if(!glfwInit())throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_VISIBLE,smoke?GLFW_FALSE:GLFW_TRUE);
        window=glfwCreateWindow(1320,760,"Rice Controller",nullptr,nullptr);
        if(!window)throw std::runtime_error("OpenGL window creation failed");
        glfwSetWindowSizeLimits(window,1100,440,GLFW_DONT_CARE,GLFW_DONT_CARE);
        glfwMakeContextCurrent(window);glfwSwapInterval(1);
        if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))throw std::runtime_error("OpenGL initialization failed");
        IMGUI_CHECKVERSION();ImGui::CreateContext();gui=true;
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        wchar_t windowsDirectory[MAX_PATH];GetWindowsDirectoryW(windowsDirectory,MAX_PATH);
        auto font=fs::path(windowsDirectory)/"Fonts/malgun.ttf";
        if(!fs::is_regular_file(font)||!io.Fonts->AddFontFromFileTTF(RC::PathText(font).c_str(),17,nullptr,io.Fonts->GetGlyphRangesKorean()))
            throw std::runtime_error("Korean system font malgun.ttf is required");
        Style();platform=ImGui_ImplGlfw_InitForOpenGL(window,true);renderer=ImGui_ImplOpenGL3_Init("#version 330");
        if(!platform||!renderer)throw std::runtime_error("ImGui backend initialization failed");
        {
            ControllerUI ui(window,config);unsigned frames=0;
            while(!ui.Done()){
                glfwPollEvents();ui.Tick();
                ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();ui.Draw();ImGui::Render();
                int width=0,height=0;glfwGetFramebufferSize(window,&width,&height);glViewport(0,0,width,height);
                glClearColor(.075f,.087f,.115f,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());glfwSwapBuffers(window);
                if(smoke&&++frames>=120){std::cout<<"Controller smoke passed (120 frames)\n";break;}
            }
        }
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(!smoke)MessageBoxW(nullptr,RC::Wide(error.what()).c_str(),L"Rice Controller",MB_OK|MB_ICONERROR);result=1;}
    if(renderer)ImGui_ImplOpenGL3_Shutdown();if(platform)ImGui_ImplGlfw_Shutdown();if(gui)ImGui::DestroyContext();
    if(window)glfwDestroyWindow(window);glfwTerminate();if(SUCCEEDED(com))CoUninitialize();return result;
}
