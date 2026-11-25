#include "ProcessManager.h"
#include "../Shared/ControlProtocol.h"
#include "Engine/Project/GameBundle.h"
#include <chrono>
#include <fstream>
#include <map>

namespace RiceController {
using Clock=std::chrono::steady_clock;
bool SupportsControl(const std::filesystem::path& executable){
    if(Rice::IsGameBundle(executable))return true;
    auto marker=executable;marker.replace_extension(".control");
    std::ifstream input(marker);std::string version,extra;
    return bool(input>>version)&&version=="1"&&!(input>>extra);
}
namespace {
bool ValidMode(const std::string& mode){return mode=="windowed"||mode=="hidden"||mode=="fullscreen";}
std::runtime_error WindowsError(const char* message){return std::runtime_error(std::string(message)+" (Win32 "+std::to_string(GetLastError())+")");}
struct WindowSearch{DWORD pid;HWND window=nullptr;};
BOOL CALLBACK FindPlayerWindow(HWND window,LPARAM parameter){
    auto& search=*reinterpret_cast<WindowSearch*>(parameter);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid!=search.pid||GetWindow(window,GW_OWNER))return TRUE;
    wchar_t kind[64]{};GetClassNameW(window,kind,64);
    if(std::wstring(kind)==L"GLFW30"){search.window=window;return FALSE;}return TRUE;
}
HWND PlayerWindow(DWORD pid){WindowSearch search{pid};EnumWindows(FindPlayerWindow,reinterpret_cast<LPARAM>(&search));return search.window;}
}
struct ProcessManager::Impl {
    struct Run {
        HANDLE process=nullptr;
        HANDLE job=nullptr;
        bool bundled=false;
        Status status;
        ControlPipe pipe;
        std::string runId,pendingCommand;
        std::unique_ptr<std::promise<void>> retirement;
        bool retirementSent=false;
        std::unique_ptr<std::promise<void>> restore;
        bool restoreSent=false;
        std::string restoreSlot;
        uint64_t nextRequest=0,pendingRequest=0;
        bool expectsControl=false,ready=false,verified=false,stopRequested=false,closeSent=false,pipeFailed=false;
        Clock::time_point started=Clock::now(),sent=started,lastStatus=started,stopTime=started;
        Run(){
            job=CreateJobObjectW(nullptr,nullptr);if(!job)throw WindowsError("Cannot create player job");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){auto error=WindowsError("Cannot configure player job");CloseHandle(job);throw error;}
        }
        ~Run(){if(job)CloseHandle(job);if(process)CloseHandle(process);}
        void VerifyPlayer(DWORD peer){
            if(peer!=status.pid){
                // A portable build starts its packaged player inside this entry's job.
                BOOL belongs=FALSE;
                HANDLE child=bundled?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,peer):nullptr;
                if(child){IsProcessInJob(child,job,&belongs);CloseHandle(child);}
                if(!belongs)throw std::runtime_error("Controller pipe connected to the wrong process");
                status.pid=peer;
            }
            verified=true;
        }
    };
    std::map<std::string,std::unique_ptr<Run>> runs;
    Run& Active(const std::string& id){auto found=runs.find(id);if(found==runs.end()||!found->second->status.alive)throw std::runtime_error("실행 중인 빌드가 없습니다.");return *found->second;}
    static void Send(Run& run,const std::string& command,const std::string& payload={}){
        if(run.retirement && command!="SaveForRetirement")throw std::runtime_error("Player retirement save is pending");
        if(run.restore && command!="RestoreSlot")throw std::runtime_error("Player slot restore is pending");
        if(!run.ready||!run.status.controlled||run.pipeFailed)throw std::runtime_error("이 빌드는 아직 제어 연결이 준비되지 않았습니다.");
        ControlMessage message;message.runId=run.runId;message.requestId=++run.nextRequest;message.command=command;message.payload=payload;
        run.pipe.Send(message);run.pendingRequest=message.requestId;run.pendingCommand=command;run.status.pending=true;run.sent=Clock::now();run.lastStatus=run.sent;
    }
    static void FailRetirement(Run& run,const std::string& text){
        if(run.retirement){run.retirement->set_exception(std::make_exception_ptr(std::runtime_error(text)));run.retirement.reset();}
        if(run.restore){run.restore->set_exception(std::make_exception_ptr(std::runtime_error(text)));run.restore.reset();}
    }
    static void SetError(Run& run,const std::string& text){FailRetirement(run,text);run.status.state=RunState::Error;run.status.error=text;run.status.pending=false;run.pendingRequest=0;}
    static void ApplyStatus(Run& run,const ControlMessage& message){
        if(message.state!="running"&&message.state!="paused"&&message.state!="stopping")throw std::runtime_error("Invalid player state response");
        if(!ValidMode(message.windowMode))throw std::runtime_error("Invalid player window mode response");
        run.status.windowMode=message.windowMode;run.status.gameFrames=message.gameFrames;run.status.renderFrames=message.renderFrames;
        run.status.scriptReady=message.scriptReady;
        if(!run.stopRequested){run.status.state=message.state=="paused"?RunState::Paused:message.state=="stopping"?RunState::Stopping:RunState::Running;run.status.error.clear();}
    }
    static void RequestClose(Run& run){
        if(run.closeSent)return;
        if(run.status.controlled&&!run.pipeFailed&&run.ready){Send(run,"Stop");run.closeSent=true;return;}
        if(auto window=PlayerWindow(run.status.pid)){if(!PostMessageW(window,WM_CLOSE,0,0))throw WindowsError("Cannot request player close");run.closeSent=true;}
    }
    static void PollRun(Run& run){
        if(!run.status.alive)return;
        if(WaitForSingleObject(run.process,0)==WAIT_OBJECT_0){
            FailRetirement(run,"Player exited before acknowledging durable retirement save");
            DWORD code=0;if(!GetExitCodeProcess(run.process,&code))throw WindowsError("Cannot read player exit code");
            run.status.alive=false;run.status.pending=false;run.status.controlled=false;run.pipe.Close();
            if(code==0){run.status.state=RunState::Stopped;run.status.error.clear();}
            else {run.status.state=RunState::Error;run.status.error="빌드 종료 코드: "+std::to_string(code);}
            return;
        }
        auto now=Clock::now();
        if(run.expectsControl&&!run.pipeFailed){
            try {
                if(run.pipe.PollConnection()){
                    if(!run.verified)run.VerifyPlayer(run.pipe.ConnectedProcessId());
                    for(int i=0;i<16;++i){
                        auto message=run.pipe.Receive();if(!message)break;
                        if(message->runId!=run.runId)continue;
                        if(message->command=="Ready"&&!run.ready){
                            if(!message->success)throw std::runtime_error(message->error);
                            ApplyStatus(run,*message);run.ready=true;run.status.controlled=true;run.status.pending=false;run.lastStatus=now;
                            run.status.canRetire=std::find(message->capabilities.begin(),message->capabilities.end(),"SaveForRetirement")!=message->capabilities.end();
                            run.status.canRestoreSlot=std::find(message->capabilities.begin(),message->capabilities.end(),"RestoreSlot")!=message->capabilities.end();
                        }else if(message->requestId&&message->requestId==run.pendingRequest){
                            if(message->command!=run.pendingCommand)throw std::runtime_error("Mismatched controller command response");
                            run.pendingRequest=0;run.status.pending=false;
                            if(!message->success)SetError(run,message->error.empty()?"빌드가 명령을 거부했습니다.":message->error);
                            else {
                                ApplyStatus(run,*message);
                                if(message->command=="SaveForRetirement" && run.retirement){
                                    if(message->payload!="durable-and-sealed")throw std::runtime_error("Invalid retirement save acknowledgement");
                                    run.retirement->set_value();run.retirement.reset();
                                }
                                if(message->command=="RestoreSlot" && run.restore){
                                    if(message->payload!="slot-restored:"+run.restoreSlot)throw std::runtime_error("Invalid slot restore acknowledgement");
                                    run.restore->set_value();run.restore.reset();
                                }
                            }
                        }
                    }
                }
            }catch(const std::exception& error){run.pipeFailed=true;run.pipe.Close();SetError(run,error.what());}
        }else if(!run.expectsControl&&!run.ready&&PlayerWindow(run.status.pid)){
            run.ready=true;run.status.state=run.stopRequested?RunState::Stopping:RunState::Running;run.status.pending=false;
        }
        if(run.stopRequested){
            RequestClose(run);
            if(now-run.stopTime>std::chrono::seconds(5))SetError(run,"정상 종료 대기 시간이 지났습니다. 강제 종료 또는 계속 대기를 선택해 주세요.");
        }else if(!run.ready&&now-run.started>std::chrono::seconds(30))SetError(run,"빌드 시작 대기 시간(30초)이 지났습니다.");
        else if(run.pendingRequest&&now-run.sent>std::chrono::seconds(run.pendingCommand=="SaveForRetirement"||run.pendingCommand=="RestoreSlot"?30:2))SetError(run,"빌드 응답이 없습니다. 종료하거나 다시 시도해 주세요.");
        else if(run.retirement && !run.retirementSent && !run.pendingRequest){Send(run,"SaveForRetirement");run.retirementSent=true;}
        else if(run.restore && !run.restoreSent && !run.pendingRequest){Send(run,"RestoreSlot",run.restoreSlot);run.restoreSent=true;}
        else if(run.ready&&run.status.controlled&&!run.pipeFailed&&!run.pendingRequest&&now-run.lastStatus>std::chrono::milliseconds(500))Send(run,"GetStatus");
    }
};
ProcessManager::ProcessManager():impl(std::make_unique<Impl>()){}
ProcessManager::~ProcessManager()=default;
void ProcessManager::Start(const Entry& source,const std::string& mode,bool silentAudio,const std::string& mainSlot){
    auto found=impl->runs.find(source.id);if(found!=impl->runs.end()&&found->second->status.alive)return;
    Entry entry=source;ValidateEntry(entry);if(!ValidMode(mode))throw std::runtime_error("Invalid window mode");
    bool controlled=SupportsControl(entry.executable);if(!controlled&&mode!="windowed")throw std::runtime_error("이 빌드는 숨김·전체화면 제어를 지원하지 않습니다.");
    auto run=std::make_unique<Impl::Run>();run->runId=NewId();run->expectsControl=controlled;run->status.windowMode=mode;
    run->bundled=Rice::IsGameBundle(entry.executable);
    run->status.state=RunState::Starting;run->status.pending=true;
    auto pipeName=L"\\\\.\\pipe\\RiceController-"+Wide(run->runId);
    std::wstring command=QuoteArgument(entry.executable.native());
    if(!entry.project.empty())command+=L" --project "+QuoteArgument(entry.project.native());
    if(silentAudio)command+=L" --silent-audio";
    if(!mainSlot.empty()){
        if(!controlled||mainSlot.size()>256)throw std::runtime_error("Invalid main slot for controlled Player launch");
        command+=L" --main-slot "+QuoteArgument(Wide(mainSlot));
    }
    if(controlled){run->pipe.OpenServer(pipeName);command+=L" --controller-pipe "+QuoteArgument(pipeName)+L" --instance-id "+QuoteArgument(Wide(run->runId))+L" --window-mode "+QuoteArgument(Wide(mode));}
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=mode=="hidden"?SW_HIDE:SW_SHOWNORMAL;
    PROCESS_INFORMATION info{};auto working=entry.executable.parent_path();
    if(!CreateProcessW(entry.executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,working.c_str(),&startup,&info))throw WindowsError("Cannot start player");
    run->process=info.hProcess;run->status.pid=info.dwProcessId;
    if(!AssignProcessToJobObject(run->job,info.hProcess)){auto error=WindowsError("Cannot assign player to controller job");TerminateProcess(info.hProcess,1);CloseHandle(info.hThread);throw error;}
    if(ResumeThread(info.hThread)==DWORD(-1)){auto error=WindowsError("Cannot resume player startup");TerminateProcess(info.hProcess,1);CloseHandle(info.hThread);throw error;}
    CloseHandle(info.hThread);run->status.alive=true;run->started=Clock::now();impl->runs[entry.id]=std::move(run);
}
void ProcessManager::Pause(const std::string& id){auto& run=impl->Active(id);if(run.status.state!=RunState::Running)throw std::runtime_error("실행 중인 빌드만 일시정지할 수 있습니다.");Impl::Send(run,"Pause");}
void ProcessManager::Resume(const std::string& id){auto& run=impl->Active(id);if(run.status.state!=RunState::Paused)throw std::runtime_error("일시정지된 빌드만 재개할 수 있습니다.");Impl::Send(run,"Resume");}
void ProcessManager::Stop(const std::string& id){
    auto& run=impl->Active(id);if(run.stopRequested)return;
    run.stopRequested=true;run.status.state=RunState::Stopping;run.status.error.clear();run.stopTime=Clock::now();run.status.pending=false;run.pendingRequest=0;Impl::RequestClose(run);
}
bool ProcessManager::StopIfCurrent(const std::string& id,const std::string& expectedRunId){
    const auto status=Get(id);
    if(!status.alive||status.runId!=expectedRunId)return false;
    Stop(id);return true;
}
std::future<void> ProcessManager::SaveForRetirement(const std::string& id,const std::string& expectedRunId){
    auto& run=impl->Active(id);
    if(run.runId!=expectedRunId || !run.status.canRetire || !run.ready || run.pipeFailed || run.stopRequested)
        throw std::runtime_error("Matching Player with retirement-save capability required");
    if(run.retirement||run.restore)throw std::runtime_error("Player data operation already pending");
    run.retirement=std::make_unique<std::promise<void>>();run.retirementSent=false;run.status.retiring=true;
    return run.retirement->get_future();
}
std::future<void> ProcessManager::RestoreSlot(const std::string& id,const std::string& expectedRunId,const std::string& mainSlot){
    auto& run=impl->Active(id);
    if(run.runId!=expectedRunId||!run.status.canRestoreSlot||run.pipeFailed||run.stopRequested||run.status.retiring)
        throw std::runtime_error("Matching Player with slot-restore capability required");
    if(run.retirement||run.restore||mainSlot.empty()||mainSlot.size()>256)throw std::runtime_error("Invalid or overlapping slot restore");
    run.restore=std::make_unique<std::promise<void>>();run.restoreSlot=mainSlot;run.restoreSent=false;return run.restore->get_future();
}
std::future<void> ProcessManager::RestoreSlotForRun(const std::string& runtime,const std::string& mainSlot){
    for(const auto& [id,run]:impl->runs)if(run->runId==runtime && run->status.alive)return RestoreSlot(id,runtime,mainSlot);
    throw std::runtime_error("Player runtime is no longer active");
}
void ProcessManager::ForceStop(const std::string& id){auto& run=impl->Active(id);if(!TerminateProcess(run.process,1))throw WindowsError("Cannot terminate player");run.status.state=RunState::Stopping;run.status.pending=false;run.stopRequested=true;run.closeSent=true;run.stopTime=Clock::now();}
void ProcessManager::ContinueWaiting(const std::string& id){auto& run=impl->Active(id);run.stopRequested=true;run.status.state=RunState::Stopping;run.status.error.clear();run.stopTime=Clock::now();run.closeSent=false;Impl::RequestClose(run);}
void ProcessManager::SetWindowMode(const std::string& id,const std::string& mode){if(!ValidMode(mode))throw std::runtime_error("Invalid window mode");auto& run=impl->Active(id);Impl::Send(run,"SetWindowMode",mode);}
void ProcessManager::Poll(){for(auto& [id,run]:impl->runs)try{Impl::PollRun(*run);}catch(const std::exception& error){Impl::SetError(*run,error.what());}}
Status ProcessManager::Get(const std::string& id)const{auto found=impl->runs.find(id);if(found==impl->runs.end())return {};auto status=found->second->status;status.runId=found->second->runId;return status;}
bool ProcessManager::AnyAlive()const{for(auto& [id,run]:impl->runs)if(run->status.alive)return true;return false;}
}
