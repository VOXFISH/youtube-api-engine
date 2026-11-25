#pragma once
#include "Engine/Data/SqliteStore.h"
#include "Engine/Data/DataContext.h"
#include "Streaming/StreamReadiness.h"

#include "../../Tools/RiceCon/Shared/ControlProtocol.h"
#include "Project/Workspace.h"
#include "Rice/Time.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>

namespace Rice {

// All window and Workspace changes are made by the player's existing main loop.
class PlayerWindowMode {
    GLFWwindow* window;
    int x=0,y=0,width=1280,height=720;
    bool fullscreen=false,escapeDown=false;
    std::string mode="windowed";

    GLFWmonitor* BestMonitor(int px,int py,int w,int h) const {
        int count=0;
        auto** monitors=glfwGetMonitors(&count);
        auto* best=glfwGetPrimaryMonitor();
        long long area=0;
        for(int i=0;i<count;++i){
            int mx=0,my=0;glfwGetMonitorPos(monitors[i],&mx,&my);
            const auto* video=glfwGetVideoMode(monitors[i]);if(!video)continue;
            auto overlap=static_cast<long long>(std::max(0,std::min(px+w,mx+video->width)-std::max(px,mx)))
                *std::max(0,std::min(py+h,my+video->height)-std::max(py,my));
            if(overlap>area){area=overlap;best=monitors[i];}
        }
        return best;
    }
    void SaveWindowedRect(){
        glfwGetWindowPos(window,&x,&y);glfwGetWindowSize(window,&width,&height);
    }
    void ClampRestoreRect(){
        auto* monitor=BestMonitor(x,y,width,height);if(!monitor)return;
        int mx=0,my=0,mw=0,mh=0;glfwGetMonitorWorkarea(monitor,&mx,&my,&mw,&mh);
        width=std::max(1,std::min(width,mw));height=std::max(1,std::min(height,mh));
        x=std::clamp(x,mx,mx+std::max(0,mw-width));
        y=std::clamp(y,my,my+std::max(0,mh-height));
    }
public:
    explicit PlayerWindowMode(GLFWwindow* native):window(native){SaveWindowedRect();}
    static bool Valid(const std::string& value){return value=="windowed"||value=="hidden"||value=="fullscreen";}
    const std::string& Get() const{return mode;}
    void Set(const std::string& value){
        if(!Valid(value))throw std::invalid_argument("Unknown window mode");
        if(value=="hidden"){glfwHideWindow(window);mode=value;return;}
        if(value=="fullscreen"){
            if(!fullscreen)SaveWindowedRect();
            int px=0,py=0,w=0,h=0;glfwGetWindowPos(window,&px,&py);glfwGetWindowSize(window,&w,&h);
            auto* monitor=BestMonitor(px,py,w,h);
            if(!monitor)throw std::runtime_error("No display is available for fullscreen");
            const auto* video=glfwGetVideoMode(monitor);
            if(!video)throw std::runtime_error("Cannot read monitor dimensions");
            glfwGetMonitorPos(monitor,&px,&py);
            glfwSetWindowAttrib(window,GLFW_DECORATED,GLFW_FALSE);
            // A monitor-less GLFW window is borderless without changing display modes.
            glfwSetWindowMonitor(window,nullptr,px,py,video->width,video->height,GLFW_DONT_CARE);
            fullscreen=true;
        }else if(fullscreen){
            ClampRestoreRect();
            glfwSetWindowAttrib(window,GLFW_DECORATED,GLFW_TRUE);
            glfwSetWindowMonitor(window,nullptr,x,y,width,height,GLFW_DONT_CARE);
            fullscreen=false;
        }
        glfwShowWindow(window);mode=value;
    }
    void Poll(){
        const bool down=glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS;
        if(down&&!escapeDown&&fullscreen&&mode!="hidden")Set("windowed");
        escapeDown=down;
    }
};

class PlayerControl {
    RiceController::ControlPipe pipe;
    std::wstring pipeName;
    std::string runId;
    PlayerWindowMode windowMode;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    bool ready=false,stopping=false;
    float resumeTimeScale=1.f;
    uint64_t lastRequest=0,gameFrames=0,renderFrames=0;
    RiceController::ControlMessage lastResponse;
    std::optional<RiceController::ControlMessage> retirementRequest;
    bool quiesced=false;

    void PollRetirement(Workspace& workspace) {
        if(!retirementRequest)return;
        auto response=State(workspace);
        response.command=retirementRequest->command;response.requestId=retirementRequest->requestId;
        try {
            // Stop runs game teardown callbacks before sealing persistent writes.
            // SQLite writes are synchronous FULL commits; no scene-file save is
            // substituted for routed game data.
            workspace.Stop();
            if(!Data::SqliteStore::TrySealProcessWrites())return;
            response.payload="durable-and-sealed";
        }catch(const std::exception& error){response.success=false;response.error=error.what();}
        lastRequest=response.requestId;lastResponse=response;retirementRequest.reset();pipe.Send(response);
    }

    RiceController::ControlMessage State(const Workspace& workspace) const {
        RiceController::ControlMessage result;
        result.runId=runId;
        result.state=stopping?"stopping":(quiesced||workspace.editor.paused)?"paused":"running";
        result.windowMode=windowMode.Get();result.gameFrames=gameFrames;result.renderFrames=renderFrames;
        result.scriptReady=Streaming::StreamReadiness::IsReady();
        return result;
    }
public:
    bool Quiesced() const noexcept { return quiesced; }
    PlayerControl(GLFWwindow* native,std::wstring connection,std::string id,const std::string& mode)
        :pipeName(std::move(connection)),runId(std::move(id)),windowMode(native){windowMode.Set(mode);}

    bool Update(Workspace& workspace){
        windowMode.Poll();
        if(pipeName.empty())return false;
        if(!ready){
            if(!pipe.OpenClient(pipeName)){
                if(std::chrono::steady_clock::now()-started>std::chrono::seconds(10))
                    throw std::runtime_error("Cannot connect to Rice Controller");
                return false;
            }
            auto message=State(workspace);message.command="Ready";
            pipe.Send(message);ready=true;
        }
        // Bound work per frame even when a sender queues many requests.
        for(int i=0;i<16;++i){
            auto input=pipe.Receive();if(!input)break;
            auto response=State(workspace);
            response.command=input->command;response.requestId=input->requestId;
            if(input->protocolVersion!=1||input->runId!=runId||!input->requestId){
                response.success=false;response.error="Invalid protocol, run or request ID";
                pipe.Send(response);continue;
            }
            if(input->requestId==lastRequest){pipe.Send(lastResponse);continue;}
            if(input->requestId<lastRequest){
                response.success=false;response.error="Stale request ID";pipe.Send(response);continue;
            }
            try{
                if(input->command=="SaveForRetirement"){
                    Streaming::StreamReadiness::Retire();
                    quiesced=true;retirementRequest=*input;PollRetirement(workspace);continue;
                }
                if(quiesced && input->command!="Stop" && input->command!="GetStatus" && input->command!="SetWindowMode")
                    throw std::runtime_error("Retired Player cannot resume game execution");
                if(input->command=="RestoreSlot"){
                    Data::DataContext::RestorePlayerSlot(input->payload);
                    auto state=State(workspace);state.command=input->command;state.requestId=input->requestId;
                    state.payload="slot-restored:"+input->payload;lastRequest=state.requestId;lastResponse=state;pipe.Send(state);continue;
                }
                if(input->command=="Pause"){
                    if(!workspace.editor.paused){
                        resumeTimeScale=Time::timeScale;workspace.Pause();Time::timeScale=0.f;
                    }
                }else if(input->command=="Resume"){
                    if(workspace.editor.paused){workspace.Resume();Time::timeScale=resumeTimeScale;}
                }else if(input->command=="Stop")stopping=true;
                else if(input->command=="SetWindowMode")windowMode.Set(input->payload);
                else if(input->command!="GetStatus")throw std::invalid_argument("Unsupported controller command");
                auto state=State(workspace);
                state.command=input->command;state.requestId=input->requestId;response=std::move(state);
            }catch(const std::exception& error){response.success=false;response.error=error.what();}
            lastRequest=input->requestId;lastResponse=response;pipe.Send(response);
            if(stopping)break;
        }
        PollRetirement(workspace);
        return stopping;
    }
    void DidUpdate(const Workspace& workspace){if(workspace.editor.playing&&!workspace.editor.paused)++gameFrames;}
    void DidRender(){++renderFrames;}
};

}
