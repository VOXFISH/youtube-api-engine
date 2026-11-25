#pragma once
#include "Rice/Benchmark.h"
#include "Project.h"
#include "Prefab.h"
#include "Scripting/NativeScriptCompiler.h"
#include "Scripting/ScriptHost.h"
#include "Rice/Debug.h"
#include <chrono>
namespace Rice {
class Workspace {
    std::map<EntityID,std::string> loadedImages;
    std::set<std::string> reported;
    bool jumpHeld=false;
    uint64_t boundScene=0;
    // Keep the decoded edit representation as the baseline. Opening an older
    // on-disk format can legitimately change the spelling of serialized fields.
    std::string savedScene;

    void ResetRuntime() {
        RICE_BENCHMARK_FUNCTION();
        audio.StopAll();
        audio.PauseAll(false);
        prefabPool.Clear(editor.scene.get());
        physics.Reset();
        loadedImages.clear();
        jumpHeld=false;
    }
public:
    AudioSystem audio;
    ComponentRegistry registry;
    EditorSession editor;
    Scope<Project> project;
    PrefabLibrary prefabs;
    PrefabPool prefabPool;
    PhysicsSystem physics;
    std::vector<std::string> logs;
    std::map<std::string,std::map<uint64_t,std::string>> runtimeScriptErrors;
    std::map<std::string,std::function<void()>> actions;
    int score=0;
    double updateMilliseconds=0;
    explicit Workspace(const fs::path& root,bool silent=false,std::function<void(ComponentRegistry&)> registerTypes={}):audio(silent),editor(registry),project(CreateScope<Project>(root)),prefabs(registry),prefabPool(registry) {
        RICE_BENCHMARK_FUNCTION();
        if(registerTypes)registerTypes(registry);
        actions["Increment"]=[this]{ RICE_BENCHMARK_FUNCTION(); ++score;Log("Score: "+std::to_string(score));};actions["Quit"]=[]{ RICE_BENCHMARK_FUNCTION(); };
        if(project->HasScene(project->ActiveScene()))editor.Replace(project->LoadScene(registry));
        savedScene=editor.DocumentSnapshot();
        prefabs.SetDirectory(project->root/"Prefabs",project->assets.Root()/"Prefabs");
        BindScene();
    }
    ~Workspace() {
        RICE_BENCHMARK_FUNCTION();
        editor.scene->SetPrefabCallbacks({},{},{});
        try { prefabPool.Clear(editor.scene.get()); } catch(...) {}
        if(editor.playing)Benchmark::EndPlay();
        // Release native continuations while the logger, registry and physics system still exist.
        editor.scene.reset();
    }
    void BindScene() {
        RICE_BENCHMARK_FUNCTION();
        if(boundScene==editor.scene->GetInstanceID())return;
        prefabPool.Clear();
        boundScene=editor.scene->GetInstanceID();
        editor.scene->SetPrefabCallbacks(
            [this](const std::string& name,Vector3 position)->GameObject& { return InstantiatePrefab(name,position); },
            [this](EntityID id) { return ReleasePrefab(id); },
            [this](const std::string& name,size_t count) { PrewarmPrefab(name,count); });
    }
    void Log(const std::string& text,spdlog::level::level_enum level=spdlog::level::info){ RICE_BENCHMARK_FUNCTION(); Debug::Init();Debug::GetClientLogger()->log(level,"{}",text);logs.push_back(text);if(logs.size()>200)logs.erase(logs.begin());}
    void Report(const std::exception& error){ RICE_BENCHMARK_FUNCTION(); if(reported.insert(error.what()).second)Log(error.what(),spdlog::level::err);}
    EntityID ImportFbx(const fs::path& source);
    void SaveScript(const fs::path& destination,const std::string& source) {
        Scripting::SaveValidatedScript(project->root,destination,source,
            [this](const std::string& warning){Log(warning,spdlog::level::warn);});
        runtimeScriptErrors.erase(Scripting::ScriptKey(project->root,destination));
    }
    bool HasUnsavedChanges() const {
        RICE_BENCHMARK_FUNCTION();
        if(project->InputBindingsChanged())return true;
        try { return editor.DocumentSnapshot()!=savedScene; }
        catch(const std::runtime_error&) { return true; } // A failed save must still allow Cancel or Discard.
    }
    void SaveBeforeClose() {
        RICE_BENCHMARK_FUNCTION();
        auto document=editor.DocumentSnapshot();
        project->SaveActiveScene(document);
        savedScene=std::move(document);
        Log("Project saved");
    }
    void Save(){
        RICE_BENCHMARK_FUNCTION();
        if(editor.playing)throw std::runtime_error("Stop play before saving");
        SaveBeforeClose();
    }
    void Open(const fs::path& path,const std::function<void()>& beforeReplace={}){
        RICE_BENCHMARK_FUNCTION();
        auto next=CreateScope<Project>(path,true);
        auto scene=next->LoadScene(registry);
        auto saved=SceneCodec(registry).Encode(*scene);
        if(beforeReplace)beforeReplace();
        Stop();
        ResetRuntime();
        project=std::move(next);
        runtimeScriptErrors.clear();
        editor.Replace(std::move(scene));
        savedScene=std::move(saved);
        prefabs.SetDirectory(project->root/"Prefabs",project->assets.Root()/"Prefabs");
        BindScene();
    }
    void New(const fs::path& path,const std::function<void()>& beforeReplace={}){
        RICE_BENCHMARK_FUNCTION();
        Open(Project::CreateNew(path),beforeReplace);
    }
    void SwitchScene(const fs::path& relative){
        RICE_BENCHMARK_FUNCTION();
        if(editor.playing)throw std::runtime_error("Stop Play before changing scenes");
        if(relative==project->ActiveScene())return;
        auto next=project->LoadScene(registry,relative);
        auto saved=SceneCodec(registry).Encode(*next);
        project->SaveScene(*editor.scene,registry,relative);
        ResetRuntime();
        editor.Replace(std::move(next));
        savedScene=std::move(saved);
        BindScene();
        Log("Scene switched");
    }
    void NewScene(const std::string& name,bool duplicate=false){
        RICE_BENCHMARK_FUNCTION();
        if(editor.playing)throw std::runtime_error("Stop Play before creating a scene");
        auto relative=Project::NamedScene(name);
        if(project->HasScene(relative))throw std::runtime_error("A scene with that name already exists");
        auto next=duplicate?SceneCodec(registry).Decode(SceneCodec(registry).Encode(*editor.scene)):CreateScope<Scene>();
        auto saved=SceneCodec(registry).Encode(*next);
        project->SaveScene(*editor.scene,registry,relative,next.get());
        ResetRuntime();
        editor.Replace(std::move(next));
        savedScene=std::move(saved);
        BindScene();
        Log("Scene created");
    }
    void Play(bool editorPreview=true){
        RICE_BENCHMARK_FUNCTION();
        if(editor.playing)return;
        ResetRuntime();
        editor.Play();
        BindScene();
        try {
            Scripting::AttachNativeScripts(*editor.scene,registry,physics,project->root,editorPreview,
                [this](int level,const std::string& message) {
                    Log(message,level==2?spdlog::level::err:level==1?spdlog::level::warn:spdlog::level::info);
                },[this](const std::string& file,uint64_t row,const std::string& message) {
                    fs::path path(std::u8string(file.begin(),file.end()));
                    runtimeScriptErrors[Scripting::ScriptKey(project->root,path)][row]=message;
                });
            runtimeScriptErrors.clear();
        } catch (...) { editor.Stop();BindScene();throw; }
        Benchmark::BeginPlay();
    }
    void Pause(){
        RICE_BENCHMARK_FUNCTION();
        if(!editor.playing||editor.paused)return;
        audio.PauseAll(true);editor.paused=true;
    }
    void Resume(){
        RICE_BENCHMARK_FUNCTION();
        if(!editor.playing||!editor.paused)return;
        audio.PauseAll(false);editor.paused=false;
    }
    void Stop(){
        RICE_BENCHMARK_FUNCTION();
        if(!editor.playing)return;
        ResetRuntime();
        editor.Stop();
        BindScene();
        Benchmark::EndPlay();
    }
    void SavePrefab(EntityID root,const std::string& name){
        RICE_BENCHMARK_FUNCTION();
        prefabs.Save(*editor.scene,root,name);
        project->assets.Refresh();
        Log("Prefab saved");
    }
    GameObject& InstantiatePrefab(const std::string& name,Vector3 position=Vector3(0)){
        RICE_BENCHMARK_FUNCTION();
        BindScene();
        if(editor.playing)return prefabPool.Instantiate(*editor.scene,prefabs.Load(name),position);
        return prefabs.Instantiate(*editor.scene,name,position);
    }
    void PrewarmPrefab(const std::string& name,size_t count){
        RICE_BENCHMARK_FUNCTION();
        if(!editor.playing)throw std::runtime_error("Start Play before warming a prefab pool");
        BindScene();
        prefabPool.Prewarm(*editor.scene,prefabs.Load(name),count);
    }
    bool ReleasePrefab(EntityID root){
        RICE_BENCHMARK_FUNCTION();
        if(!editor.playing)return false;
        return prefabPool.Release(*editor.scene,root);
    }
    void PlayAudio(AudioSource& source){ RICE_BENCHMARK_FUNCTION(); source.Play(audio,ProjectFile::Utf8(project->assets.Resolve(source.clip)));}
    void Update(float dt,bool captureKeyboard){
        RICE_BENCHMARK_FUNCTION();
        BindScene();
        auto begin=std::chrono::steady_clock::now();
        project->input.BeginFrame(captureKeyboard||editor.paused);
        if(editor.playing&&!editor.paused){
            bool jump=!captureKeyboard&&project->input.Held("Jump");
            for(auto& obj:editor.scene->GetObjects())if(obj->IsActive())if(auto* c=obj->GetComponent<PlayerController>();c&&c->enabled&&!captureKeyboard){Vector3 direction{project->input.Axis("Left","Right"),0,project->input.Axis("Forward","Backward")};if(glm::length(direction)>1)direction=glm::normalize(direction);
                if(auto* r=obj->GetComponent<Rigidbody>();r&&r->enabled){r->velocity.x=direction.x*c->speed;r->velocity.z=direction.z*c->speed;if(jump&&!jumpHeld)r->velocity.y=5;}else obj->TranslateWorld(direction*c->speed*std::min(dt,.1f));}
            jumpHeld=jump;
            editor.scene->Update(std::min(dt,.1f));physics.Update(*editor.scene,dt);editor.scene->LateUpdate(dt);
        }
        auto& scene=*editor.scene;
        auto sky=scene.settings.skybox?scene.settings.skyboxAsset:std::string{};
        if(sky!=scene.loadedSkyboxAsset){
            scene.skyboxTexture.reset();scene.loadedSkyboxAsset=sky;
            try{if(!sky.empty())scene.skyboxTexture=project->assets.Texture(sky);}catch(const std::exception& error){Report(error);}
        }
        for(auto& obj:editor.scene->GetObjects()){
            if(auto* mesh=obj->GetComponent<MeshRenderer>())for(auto& [index,value]:mesh->triangleMaterials.faces)if(value.albedoAsset!=value.loadedAsset){
                value.loadedAsset=value.albedoAsset;value.albedoTexture.reset();
                try{if(!value.albedoAsset.empty())value.albedoTexture=project->assets.Texture(value.albedoAsset);}catch(const std::exception& error){Report(error);}
            }
            if(auto* material=obj->GetComponent<Material>();material && material->albedoAsset!=material->loadedAsset){
                try{
                    material->albedoTexture=material->albedoAsset.empty()?nullptr:project->assets.Texture(material->albedoAsset);
                    material->loadedAsset=material->albedoAsset;
                }catch(const std::exception& error){
                    material->albedoTexture.reset();
                    material->loadedAsset.clear();
                    Report(error);
                }
            }
            if(auto* s=obj->GetComponent<SpriteRenderer>()){
                try{if(s->asset.empty()){s->texture.reset();loadedImages.erase(obj->GetID());}else if(!s->texture||loadedImages[obj->GetID()]!=s->asset){s->texture=project->assets.Texture(s->asset);loadedImages[obj->GetID()]=s->asset;}}catch(const std::exception& e){s->texture.reset();Report(e);}
                s->uvRect={0,0,1,1};if(auto* a=obj->GetComponent<SpriteAnimator>();a&&a->enabled){int columns=std::clamp(a->columns,1,1024),rows=std::clamp(a->rows,1,1024);int frame=std::clamp(a->frame,0,columns*rows-1);s->uvRect={float(frame%columns)/columns,1.f-float(frame/columns+1)/rows,1.f/columns,1.f/rows};}
            }
            if(auto* a=obj->GetComponent<AudioSource>()){
                try{if(!obj->IsActive()||!a->enabled){a->Stop();continue;}if(editor.playing&&!editor.paused&&!a->attempted){a->attempted=true;if(a->playOnStart&&!a->clip.empty())PlayAudio(*a);}a->Sync();}catch(const std::exception& e){Report(e);}
            }
        }
        editor.scene->Flush();std::erase_if(loadedImages,[&](const auto& pair){ RICE_BENCHMARK_FUNCTION(); return !editor.scene->Find(pair.first);});audio.Update();updateMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    }
};
}
