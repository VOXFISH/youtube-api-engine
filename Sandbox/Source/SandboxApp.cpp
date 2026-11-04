#include <Rice.h>
#include "Editor/SceneInspector.h"
#include "../Scripts/GameComponents.h"
#include <Windows.h>
#include <iostream>
#include <filesystem>
namespace {
struct Options {std::filesystem::path binaries,project,exportTo;bool smoke=false,silent=false,gizmoSmoke=false,groupSmoke=false;} options;
class SandboxApp:public Rice::Application {
    Rice::Scope<Rice::Workspace> workspace;
    Rice::SceneInspector inspector;
    Rice::Scope<Rice::SceneRenderer> renderer;
    Rice::PerspectiveCamera camera{45,16.f/9,.1f,100};
    int frames=0;
    void OpenProject(const std::filesystem::path& path){
        RICE_BENCHMARK_FUNCTION();
        auto root=Rice::Project::ResolveSavedRoot(path);
        auto next=Rice::CreateScope<Rice::Workspace>(root,options.silent,RegisterGameComponents);
        next->actions["Quit"]=[this]{Close();};
        workspace=std::move(next);options.project=root;
    }
    void OnInit() override {
        RICE_BENCHMARK_FUNCTION();
        Rice::RenderCommand::SetDepthTestEnabled(true);
        if(!options.project.empty())OpenProject(options.project);
#ifdef RICE_PLAYER
        if(!workspace)throw std::runtime_error("Player requires a saved project");
        workspace->Play();
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
            Rice::GameUI::Draw(*workspace);
#else
            if(!workspace){
                if(auto path=inspector.DrawEmpty(options.binaries)){
                    try{OpenProject(*path);}catch(const std::exception& error){inspector.ReportOpenError(error.what());RICE_CORE_ERROR("{}",error.what());}
                }
                return;
            }
            inspector.Draw(*workspace,options.binaries,[this](int width,int height,bool game){ RICE_BENCHMARK_FUNCTION(); camera.SetPerspective(45,float(width)/height,.1f,100);Rice::Camera& viewCamera=game?Rice::CameraComponent::GameView(*workspace->editor.scene,float(width)/height,camera):inspector.SceneCamera(width,height);renderer->Render(*workspace->editor.scene,viewCamera,false,0,!game);workspace->editor.scene->Render();return viewCamera.GetVPMatrix();});
#endif
        });PushOverlay(gui);
        if(!options.exportTo.empty()){if(!workspace)throw std::runtime_error("Export requires --project");workspace->Stop();workspace->project->Export(*workspace->editor.scene,workspace->registry,options.binaries,options.exportTo);Close();}
        if(options.smoke&&workspace){for(auto& obj:workspace->editor.scene->GetObjects())if(auto* a=obj->GetComponent<Rice::AudioSource>();a&&!a->clip.empty()){workspace->PlayAudio(*a);break;}}
    }
    void OnUpdate(Rice::Time dt) override {
        RICE_BENCHMARK_FUNCTION();
        try{if(workspace)workspace->Update(dt.GetSeconds(),ImGui::GetCurrentContext()&&(ImGui::GetIO().WantTextInput
#ifndef RICE_PLAYER
            || inspector.CapturesGameInput()
#endif
        ));}catch(const std::exception& error){workspace->Log(error.what(),spdlog::level::err);if(options.smoke)throw;workspace->Stop();}
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
        auto& window=GetWindow();if(window.GetHeight())camera.SetPerspective(45,float(window.GetWidth())/window.GetHeight(),.1f,100);renderer->Render(*workspace->editor.scene,Rice::CameraComponent::GameView(*workspace->editor.scene,float(window.GetWidth())/std::max(1u,window.GetHeight()),camera));workspace->editor.scene->Render();
#endif
    }
};
}
int main(int argc,char** argv){
    Rice::Debug::Init();
    try{
        wchar_t path[32768];auto count=GetModuleFileNameW(nullptr,path,32768);if(!count||count>=32768)throw std::runtime_error("Cannot resolve executable path");options.binaries=std::filesystem::path(path).parent_path();
#ifdef RICE_PLAYER
        options.project=options.binaries/"Project";
#endif
        for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--smoke")options.smoke=true;else if(arg=="--gizmo-smoke"){options.smoke=true;options.gizmoSmoke=true;}else if(arg=="--group-smoke"){options.smoke=true;options.gizmoSmoke=true;options.groupSmoke=true;}else if(arg=="--silent-audio")options.silent=true;else if(arg=="--project"&&i+1<argc)options.project=std::filesystem::absolute(argv[++i]);else if(arg=="--export"&&i+1<argc)options.exportTo=std::filesystem::absolute(argv[++i]);else throw std::runtime_error("Usage: [--project folder-or-project.rice] [--export new-folder] [--smoke] [--silent-audio]");}
        Rice::BenchmarkSession benchmark(options.binaries/"Logs"/"functions.tsv");
        SandboxApp app;app.Run();return 0;
    }catch(const std::exception& error){RICE_CORE_ERROR("Fatal: {}",error.what());if(options.smoke){std::cerr<<error.what()<<'\n';for(const auto& entry:Rice::Debug::GetMessages())std::cerr<<entry.text<<'\n';}if(!options.smoke)MessageBoxA(nullptr,error.what(),"Rice - Startup/runtime error",MB_OK|MB_ICONERROR);return 1;}
}
