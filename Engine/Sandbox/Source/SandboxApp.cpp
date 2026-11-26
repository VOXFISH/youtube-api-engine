#include <Rice.h>
#include "Editor/SceneInspector.h"
#include "../Scripts/GameComponents.h"
#include <Windows.h>
#include <shellapi.h>
#include <iostream>
#include <filesystem>
#ifdef RICE_PLAYER
#include "PlayerControl.h"
#include "PlayerFrameCapture.h"
#include "Renderer/ViewportTarget.h"
#else
#include "Platform/Windows/UiFont.h"
#endif
namespace {
struct Options {
    std::filesystem::path binaries,project,exportTo;
    bool smoke=false,silent=false,gizmoSmoke=false,groupSmoke=false,controllerRequested=false,createProject=false;
    std::wstring controllerPipe;
    std::string instanceId,windowMode="windowed";
    std::string mainSlot;
} options;
std::string Utf8(const std::wstring& value){
    if(value.empty())return {};
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!size)throw std::runtime_error("Invalid Unicode argument");
    std::string result(size,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);
    return result;
}
void ReadOptions(){
    int count=0;auto** raw=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!raw)throw std::runtime_error("Cannot read command line");
    struct Release {wchar_t** value;~Release(){LocalFree(value);}} release{raw};
    for(int i=1;i<count;++i)if(std::wstring(raw[i])==L"--controller-pipe")options.controllerRequested=true;
    for(int i=1;i<count;++i){
        const std::wstring arg=raw[i];
        const auto next=[&]() -> std::wstring {if(i+1>=count)throw std::runtime_error("Missing value for "+Utf8(arg));return raw[++i];};
        if(arg==L"--smoke")options.smoke=true;
        else if(arg==L"--gizmo-smoke"){options.smoke=true;options.gizmoSmoke=true;}
        else if(arg==L"--group-smoke"){options.smoke=true;options.gizmoSmoke=true;options.groupSmoke=true;}
        else if(arg==L"--silent-audio")options.silent=true;
        else if(arg==L"--project")options.project=std::filesystem::absolute(next());
#ifndef RICE_PLAYER
        else if(arg==L"--new-project"){
            options.project=std::filesystem::absolute(next());
            options.createProject=true;
        }
        else if(!arg.empty() && arg.front()!=L'-' && options.project.empty() && std::filesystem::path(arg).extension()==L".rice")
            options.project=std::filesystem::absolute(arg);
#endif
        else if(arg==L"--export")options.exportTo=std::filesystem::absolute(next());
#ifdef RICE_PLAYER
        else if(arg==L"--controller-pipe")options.controllerPipe=next();
        else if(arg==L"--instance-id")options.instanceId=Utf8(next());
        else if(arg==L"--window-mode")options.windowMode=Utf8(next());
        else if(arg==L"--main-slot")options.mainSlot=Utf8(next());
#endif
        else throw std::runtime_error("Unknown argument: "+Utf8(arg));
    }
#ifdef RICE_PLAYER
    if(!Rice::PlayerWindowMode::Valid(options.windowMode))throw std::runtime_error("Window mode must be windowed, hidden or fullscreen");
    if(options.controllerPipe.empty()!=options.instanceId.empty())throw std::runtime_error("Controller pipe and instance ID must be supplied together");
    if(!options.controllerPipe.empty()){
        if(options.controllerPipe.rfind(L"\\\\.\\pipe\\",0)!=0)throw std::runtime_error("Controller pipe must be local");
        if(options.instanceId.size()>80||!std::all_of(options.instanceId.begin(),options.instanceId.end(),[](unsigned char ch){return (ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_';}))
            throw std::runtime_error("Invalid controller instance ID");
    }
#endif
}
class SandboxApp:public Rice::Application {
    Rice::Scope<Rice::Workspace> workspace;
    Rice::SceneInspector inspector;
    Rice::Scope<Rice::SceneRenderer> renderer;
    Rice::PerspectiveCamera camera{45,16.f/9,.1f,100};
    int frames=0;
#ifndef RICE_PLAYER
    bool closeProjectAfterFrame=false;
#endif
#ifdef RICE_PLAYER
    Rice::Scope<Rice::PlayerControl> playerControl;
    std::shared_ptr<RiceController::PlayerMediaChannel> mediaChannel;
    Rice::Scope<Rice::PlayerFrameCapture> frameCapture;
    Rice::ViewportTarget streamTarget;
#endif
public:
    SandboxApp():Rice::Application(Rice::WindowProps("Rice",1920,1080,
        options.windowMode=="windowed" && !options.smoke && options.exportTo.empty())){}
private:
#ifndef RICE_PLAYER
    void OnCloseRequested() override {
        if(workspace)inspector.RequestExit();
        else Close();
    }
#endif
    void OpenProject(const std::filesystem::path& path){
        RICE_BENCHMARK_FUNCTION();
        auto root=Rice::Project::ResolveSavedRoot(path);
        auto next=Rice::CreateScope<Rice::Workspace>(root,options.silent,RegisterGameComponents);
        next->actions["Quit"]=[this]{OnCloseRequested();};
        workspace=std::move(next);options.project=root;
    }
    void OnInit() override {
        RICE_BENCHMARK_FUNCTION();
#ifdef RICE_PLAYER
        if(!options.mainSlot.empty())Rice::Data::DataContext::ConfigurePlayerMainSlot(options.mainSlot);
#endif
        Rice::RenderCommand::SetDepthTestEnabled(true);
        if(!options.project.empty())
            OpenProject(options.createProject ? Rice::Project::CreateNew(options.project) : options.project);
#ifdef RICE_PLAYER
        if(!workspace)throw std::runtime_error("Player requires a saved project");
        Rice::Streaming::StreamReadiness::BeginInstance();
        workspace->Play(false);
#endif
        if(options.smoke&&workspace&&!options.gizmoSmoke&&!workspace->editor.playing)workspace->Play();
        if(options.gizmoSmoke){
            if(!workspace)throw std::runtime_error("Gizmo/group smoke requires --project");
            if(!workspace->editor.scene->GetObjects().empty())workspace->editor.selected=workspace->editor.scene->GetObjects().front()->GetID();
        }
        if(options.groupSmoke){std::set<Rice::EntityID> members;for(auto& obj:workspace->editor.scene->GetObjects())if(obj->GetComponent<Rice::MeshRenderer>())members.insert(obj->GetID());
            auto group=Rice::EditorGroups::Create(workspace->editor,members);if(!group)throw std::runtime_error("Group smoke needs mesh objects");
            workspace->editor.scene->Find(group)->transform.rotation.y=20;
        }
        renderer=Rice::CreateScope<Rice::SceneRenderer>();camera.SetPosition({0,4,10});camera.LookAtObj({0,0,0});
        auto* gui=new Rice::ImGuiLayer();gui->SetRenderCallback([this]{
            RICE_BENCHMARK_FUNCTION();
#ifdef RICE_PLAYER
            if(playerControl && playerControl->Quiesced())return;
            Rice::GameUI::Draw(*workspace);
#else
            if(!workspace){
                if(auto request=inspector.DrawEmpty(options.binaries)){
                    try{OpenProject(request->create ? Rice::Project::CreateNew(request->path) : request->path);}
                    catch(const std::exception& error){inspector.ReportOpenError(error.what());RICE_CORE_ERROR("{}",error.what());}
                }
                return;
            }
            auto result=inspector.Draw(*workspace,options.binaries,[this](int width,int height,bool game){
                RICE_BENCHMARK_FUNCTION();
                const float aspect=float(width)/height;
                camera.SetPerspective(45,aspect,.1f,100);
                auto& scene=*workspace->editor.scene;
                Rice::Camera& viewCamera=game?Rice::CameraComponent::GameView(scene,aspect,camera):inspector.SceneCamera(width,height);
                renderer->Render(scene,viewCamera,false,0,!game);
                ObservePresentation(scene,*renderer);
                scene.Render();
                return viewCamera.GetVPMatrix();
            });
            if(result==Rice::SceneInspector::CloseResult::Exit)Close();
            if(result==Rice::SceneInspector::CloseResult::CloseProject)closeProjectAfterFrame=true;
#endif
        });
#ifdef RICE_PLAYER
        if(!options.controllerPipe.empty()){
            mediaChannel=std::make_shared<RiceController::PlayerMediaChannel>(options.instanceId,true);
            frameCapture=Rice::CreateScope<Rice::PlayerFrameCapture>(mediaChannel);
            workspace->audio.CaptureGameMix([channel=mediaChannel](std::span<const float> audio){channel->PublishAudio(audio);});
            gui->SetCanvasSize(1920,1080);
        }
#endif
        PushOverlay(gui);
#ifndef RICE_PLAYER
        Rice::ApplyMapleStoryFont(13.0f);
#endif
        if(!options.exportTo.empty()){if(!workspace)throw std::runtime_error("Export requires --project");workspace->Stop();workspace->project->Export(*workspace->editor.scene,workspace->registry,options.binaries,options.exportTo);Close();}
        if(options.smoke&&workspace){for(auto& obj:workspace->editor.scene->GetObjects())if(auto* a=obj->GetComponent<Rice::AudioSource>();a&&!a->clip.empty()){workspace->PlayAudio(*a);break;}}
#ifdef RICE_PLAYER
        playerControl=Rice::CreateScope<Rice::PlayerControl>(static_cast<GLFWwindow*>(GetWindow().GetNativeWindow()),options.controllerPipe,options.instanceId,options.windowMode);
#endif
    }
    void OnUpdate(Rice::Time) override {
        RICE_BENCHMARK_FUNCTION();
#ifdef RICE_PLAYER
        if(playerControl->Update(*workspace)){Close();return;}
        if(playerControl->Quiesced())return;
#endif
        try{if(workspace)workspace->Update(Rice::Time::unscaledDeltaTime,ImGui::GetCurrentContext()&&(ImGui::GetIO().WantTextInput
#ifndef RICE_PLAYER
            || inspector.CapturesGameInput()
#endif
        ));}catch(const std::exception& error){workspace->Log(error.what(),spdlog::level::err);if(options.smoke||options.controllerRequested)throw;workspace->Stop();}
#ifdef RICE_PLAYER
        playerControl->DidUpdate(*workspace);
#endif
        if(options.smoke&&++frames>=120){
            auto logs=Rice::Debug::GetMessages();
            bool captured=std::any_of(logs.begin(),logs.end(),[](const auto& entry){ RICE_BENCHMARK_FUNCTION(); return entry.text.find("OpenGL Info")!=std::string::npos;});
            if(!captured)throw std::runtime_error("Startup log missing from editor log buffer");
            for(const auto& entry:logs)std::cout<<entry.text<<'\n';
            std::cout<<"Smoke passed: "<<(workspace?"project loaded":"empty editor")<<" (120 frames)\n";
            Close();
        }
    }
    void OnRender() override {
        RICE_BENCHMARK_FUNCTION();
#ifdef RICE_PLAYER
        if(playerControl->Quiesced())return;
        if(frameCapture){
            streamTarget.Render(1920,1080,[&]{
                camera.SetPerspective(45,1920.f/1080.f,.1f,100);
                renderer->Render(*workspace->editor.scene,Rice::CameraComponent::GameView(*workspace->editor.scene,1920.f/1080.f,camera));
                ObservePresentation(*workspace->editor.scene,*renderer);
                workspace->editor.scene->Render();
            });
            // Keep this target bound for the subsequent game GUI pass.
            glBindFramebuffer(GL_FRAMEBUFFER,streamTarget.Framebuffer());glViewport(0,0,1920,1080);
            playerControl->DidRender();return;
        }
        auto& window=GetWindow();if(window.GetHeight())camera.SetPerspective(45,float(window.GetWidth())/window.GetHeight(),.1f,100);renderer->Render(*workspace->editor.scene,Rice::CameraComponent::GameView(*workspace->editor.scene,float(window.GetWidth())/std::max(1u,window.GetHeight()),camera));ObservePresentation(*workspace->editor.scene,*renderer);workspace->editor.scene->Render();
        playerControl->DidRender();
#endif
    }
    void OnFrameRendered() override {
#ifndef RICE_PLAYER
        // ImGui's draw lists can reference textures belonging to the last open scene.
        if(closeProjectAfterFrame){
            closeProjectAfterFrame=false;
            workspace.reset();
            options.project.clear();
        }
#else
        if(!frameCapture || playerControl->Quiesced())return;
        frameCapture->Capture();
        auto* window=static_cast<GLFWwindow*>(GetWindow().GetNativeWindow());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
        if(glfwGetWindowAttrib(window,GLFW_VISIBLE)&&!glfwGetWindowAttrib(window,GLFW_ICONIFIED)){
            int width=0,height=0;glfwGetFramebufferSize(window,&width,&height);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,streamTarget.Framebuffer());
            glBlitFramebuffer(0,0,1920,1080,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_LINEAR);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,0);
#endif
    }
};
}
int main(int argc,char** argv){
    (void)argc;(void)argv;
    Rice::Debug::Init();
    try{
        wchar_t path[32768];auto count=GetModuleFileNameW(nullptr,path,32768);if(!count||count>=32768)throw std::runtime_error("Cannot resolve executable path");options.binaries=std::filesystem::path(path).parent_path();
#ifdef RICE_PLAYER
        options.project=options.binaries/"Project";
#endif
        ReadOptions();
        const auto logs=options.instanceId.empty()?options.binaries/"Logs":options.binaries/"Logs"/options.instanceId;
        Rice::BenchmarkSession benchmark(logs/"functions.tsv");
        SandboxApp app;app.Run();return 0;
    }catch(const std::exception& error){RICE_CORE_ERROR("Fatal: {}",error.what());if(options.smoke||options.controllerRequested){std::cerr<<error.what()<<'\n';for(const auto& entry:Rice::Debug::GetMessages())std::cerr<<entry.text<<'\n';}else MessageBoxA(nullptr,error.what(),"Rice - Startup/runtime error",MB_OK|MB_ICONERROR);return 1;}
}
