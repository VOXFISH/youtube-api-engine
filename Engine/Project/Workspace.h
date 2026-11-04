#pragma once
#include "Rice/Benchmark.h"
#include "SceneCodec.h"
#include "Prefab.h"
#include "Rice/Debug.h"
#include <chrono>
namespace Rice {
class Project {
    fs::path activeScene = "main.rice";

    fs::path ValidateScenePath(const fs::path& relative) const {
        RICE_BENCHMARK_FUNCTION();
        if(relative.is_absolute() || relative.empty() || relative.extension() != ".rice")
            throw std::invalid_argument("Choose a project scene file");
        if(relative != "main.rice" && relative.parent_path() != "Scenes")
            throw std::invalid_argument("Scenes belong in the project's Scenes folder");
        auto target = fs::weakly_canonical(root / relative);
        auto inside = target.lexically_relative(fs::weakly_canonical(root));
        if(inside.empty() || *inside.begin() == "..")
            throw std::invalid_argument("Scene path escapes the project");
        return target;
    }
public:
    fs::path root;
    AssetDatabase assets;
    InputActions input;
    // Resolve only saved projects. Opening a typo must never create Assets or
    // turn an arbitrary scene file into a new project directory.
    static fs::path ResolveSavedRoot(const fs::path& selected){
        RICE_BENCHMARK_FUNCTION();
        if(selected.empty())throw std::invalid_argument("Choose a saved project");
        auto directory=fs::absolute(selected).lexically_normal();
        if(fs::is_regular_file(directory)){
            if(directory.filename()!="project.rice"&&directory.filename()!="main.rice")
                throw std::invalid_argument("Choose project.rice, or a legacy main.rice project");
            directory=directory.parent_path();
        }
        if(!fs::is_directory(directory)||(!fs::is_regular_file(directory/"project.rice")&&!fs::is_regular_file(directory/"main.rice")))
            throw std::runtime_error("The selected path is not a saved Rice project");
        fs::path active="main.rice";
        if(fs::is_regular_file(directory/"project.rice")){
            std::istringstream in(ReadFile(directory/"project.rice"));std::string magic;int version=0,count=0;
            in>>magic>>version>>count;
            if(magic!="RICE_PROJECT"||(version!=1&&version!=2)||count<0||count>128)throw std::runtime_error("Invalid project file");
            for(int i=0;i<count;++i){std::string name;int key=0;in>>std::quoted(name)>>key;if(!in||key<32||key>348)throw std::runtime_error("Invalid input binding");}
            if(version==2){std::string marker,sceneName;in>>marker>>std::quoted(sceneName);
                if(!in||marker!="ActiveScene")throw std::runtime_error("Invalid active scene");
                active=fs::path(std::u8string(sceneName.begin(),sceneName.end()));}
        }
        if(active.is_absolute()||active.empty()||active.extension()!=".rice"||(active!="main.rice"&&active.parent_path()!="Scenes"))
            throw std::runtime_error("Invalid active scene path");
        auto target=fs::weakly_canonical(directory/active);auto relative=target.lexically_relative(fs::weakly_canonical(directory));
        if(relative.empty()||*relative.begin()==".."||!fs::is_regular_file(target))throw std::runtime_error("The active project scene is missing or outside the project");
        return directory;
    }
    explicit Project(fs::path directory):root(fs::absolute(directory)),assets(root/"Assets") {
        RICE_BENCHMARK_FUNCTION();
        if(fs::exists(root/"project.rice")){
            std::istringstream in(ReadFile(root/"project.rice"));std::string magic;int version=0,count=0;in>>magic>>version>>count;
            if(magic!="RICE_PROJECT"||(version!=1&&version!=2)||count<0||count>128)throw std::runtime_error("Invalid project file");
            input.keys.clear();for(int i=0;i<count;++i){std::string name;int key;in>>std::quoted(name)>>key;if(!in||key<32||key>348)throw std::runtime_error("Invalid input binding");input.keys[name]=key;}
            if(version==2){
                std::string marker,selected;
                in>>marker>>std::quoted(selected);
                if(!in||marker!="ActiveScene")throw std::runtime_error("Invalid active scene");
                activeScene=fs::path(std::u8string(selected.begin(),selected.end()));
                ValidateScenePath(activeScene);
            }
        }
    }
    void Save(){
        RICE_BENCHMARK_FUNCTION();
        std::ostringstream out;
        out<<"RICE_PROJECT 2 "<<input.keys.size()<<'\n';
        for(auto& [name,key]:input.keys)out<<std::quoted(name)<<' '<<key<<'\n';
        auto utf8=activeScene.generic_u8string();
        out<<"ActiveScene "<<std::quoted(std::string(utf8.begin(),utf8.end()))<<'\n';
        AtomicWrite(root/"project.rice",out.str());
    }
    const fs::path& ActiveScene() const { RICE_BENCHMARK_FUNCTION(); return activeScene; }
    fs::path ScenePath() const { RICE_BENCHMARK_FUNCTION(); return ValidateScenePath(activeScene); }
    fs::path ScenePath(const fs::path& relative) const { RICE_BENCHMARK_FUNCTION(); return ValidateScenePath(relative); }
    std::vector<fs::path> SceneFiles() const {
        RICE_BENCHMARK_FUNCTION();
        std::vector<fs::path> result;
        if(fs::is_regular_file(root/"main.rice"))result.emplace_back("main.rice");
        if(fs::is_directory(root/"Scenes"))for(const auto& file:fs::directory_iterator(root/"Scenes")){
            if(file.is_regular_file()&&file.path().extension()==".rice"){
                auto relative=fs::path("Scenes")/file.path().filename();
                ValidateScenePath(relative);
                result.push_back(std::move(relative));
            }
        }
        std::sort(result.begin(),result.end());
        return result;
    }
    static fs::path NamedScene(const std::string& name) {
        RICE_BENCHMARK_FUNCTION();
        fs::path filename(std::u8string(name.begin(),name.end()));
        if(name.empty()||name.find_first_not_of(" \t\r\n")==std::string::npos||
           filename.has_parent_path()||name.find_first_of("<>:\"/\\|?*")!=std::string::npos)
            throw std::invalid_argument("Enter a scene name without path separators");
        if(filename.extension()!=".rice")filename+=".rice";
        return fs::path("Scenes")/filename;
    }
    void SelectScene(const fs::path& relative) {
        RICE_BENCHMARK_FUNCTION();
        if(!fs::is_regular_file(ValidateScenePath(relative)))throw std::runtime_error("Scene file does not exist");
        auto previous=activeScene;
        activeScene=relative;
        try { Save(); } catch(...) { activeScene=std::move(previous);throw; }
    }
    void Export(Scene& scene,ComponentRegistry& registry,const fs::path& binaries,const fs::path& destination){
        RICE_BENCHMARK_FUNCTION();
        auto target=fs::absolute(destination).lexically_normal();if(fs::exists(target))throw std::runtime_error("Choose a new, empty export directory");
        auto relative=target.lexically_relative(root.lexically_normal());if(!relative.empty()&&*relative.begin()!="..")throw std::runtime_error("Export outside the project directory");
        if(!fs::is_regular_file(binaries/"RicePlayer.exe"))throw std::runtime_error("Build RicePlayer first");
        for(auto& obj:scene.GetObjects()){
            if(auto* material=obj->GetComponent<Material>();material&&!material->albedoAsset.empty())assets.Resolve(material->albedoAsset);
            if(auto* mesh=obj->GetComponent<MeshRenderer>())for(const auto& [index,value]:mesh->triangleMaterials.faces)if(!value.albedoAsset.empty())assets.Resolve(value.albedoAsset);
            if(auto* sprite=obj->GetComponent<SpriteRenderer>();sprite&&!sprite->asset.empty())assets.Resolve(sprite->asset);
            if(auto* sound=obj->GetComponent<AudioSource>();sound&&!sound->clip.empty())assets.Resolve(sound->clip);
        }
        if(scene.settings.skybox&&!scene.settings.skyboxAsset.empty())assets.Resolve(scene.settings.skyboxAsset);
        auto sceneText=SceneCodec(registry).Encode(scene,false);
        fs::create_directories(target);
        for(auto& file:fs::directory_iterator(binaries))if(file.is_regular_file()&&(file.path().extension()==".dll"||file.path().filename()=="RicePlayer.exe"))fs::copy_file(file.path(),target/file.path().filename());
        fs::create_directories(target/"Project");fs::copy(root/"Assets",target/"Project"/"Assets",fs::copy_options::recursive);
        for(const auto& sceneFile:SceneFiles()){
            if(sceneFile==activeScene)continue;
            auto destinationScene=target/"Project"/sceneFile;
            fs::create_directories(destinationScene.parent_path());
            auto other=SceneCodec(registry).Load(ScenePath(sceneFile));
            AtomicWrite(destinationScene,SceneCodec(registry).Encode(*other,false));
        }
        AtomicWrite(target/"Project"/activeScene,sceneText);
        Save();fs::copy_file(root/"project.rice",target/"Project"/"project.rice");
        AtomicWrite(target/"README.txt","Run RicePlayer.exe. Project assets and FMOD runtime must remain beside the player.\n");
    }
};
class Workspace {
    std::map<EntityID,std::string> loadedImages;
    std::set<std::string> reported;
    bool jumpHeld=false;
    uint64_t boundScene=0;

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
    std::map<std::string,std::function<void()>> actions;
    int score=0;
    double updateMilliseconds=0;
    explicit Workspace(const fs::path& root,bool silent=false,std::function<void(ComponentRegistry&)> registerTypes={}):audio(silent),editor(registry),project(CreateScope<Project>(root)),prefabs(registry),prefabPool(registry) {
        RICE_BENCHMARK_FUNCTION();
        if(registerTypes)registerTypes(registry);
        actions["Increment"]=[this]{ RICE_BENCHMARK_FUNCTION(); ++score;Log("Score: "+std::to_string(score));};actions["Quit"]=[]{ RICE_BENCHMARK_FUNCTION(); };
        if(fs::exists(project->ScenePath()))editor.Load(project->ScenePath());
        else if(fs::exists(project->root/"project.rice"))throw std::runtime_error("The active project scene is missing");
        prefabs.SetDirectory(project->assets.Root()/"Prefabs");
        BindScene();
    }
    ~Workspace() {
        RICE_BENCHMARK_FUNCTION();
        editor.scene->SetPrefabCallbacks({},{},{});
        try { prefabPool.Clear(editor.scene.get()); } catch(...) {}
        if(editor.playing)Benchmark::EndPlay();
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
    void Save(){ RICE_BENCHMARK_FUNCTION(); if(editor.playing)throw std::runtime_error("Stop play before saving");SceneCodec(registry).Save(*editor.scene,project->ScenePath());project->Save();Log("Project saved");}
    void Open(const fs::path& path){
        RICE_BENCHMARK_FUNCTION();
        auto next=CreateScope<Project>(Project::ResolveSavedRoot(path));
        auto scene=SceneCodec(registry).Load(next->ScenePath());
        Stop();
        ResetRuntime();
        project=std::move(next);
        editor.Replace(std::move(scene));
        prefabs.SetDirectory(project->assets.Root()/"Prefabs");
        BindScene();
    }
    void New(const fs::path& path){
        RICE_BENCHMARK_FUNCTION();
        if(fs::exists(path))throw std::runtime_error("Use a new project folder");
        auto next=CreateScope<Project>(path);
        Scene empty;
        SceneCodec(registry).Save(empty,next->ScenePath());
        next->Save();
        Open(path);
    }
    void SwitchScene(const fs::path& relative){
        RICE_BENCHMARK_FUNCTION();
        if(editor.playing)throw std::runtime_error("Stop Play before changing scenes");
        if(relative==project->ActiveScene())return;
        auto next=SceneCodec(registry).Load(project->ScenePath(relative));
        Save();
        ResetRuntime();
        project->SelectScene(relative);
        editor.Replace(std::move(next));
        BindScene();
        Log("Scene switched");
    }
    void NewScene(const std::string& name,bool duplicate=false){
        RICE_BENCHMARK_FUNCTION();
        if(editor.playing)throw std::runtime_error("Stop Play before creating a scene");
        auto relative=Project::NamedScene(name);
        auto target=project->ScenePath(relative);
        if(fs::exists(target))throw std::runtime_error("A scene with that name already exists");
        auto next=duplicate?SceneCodec(registry).Decode(SceneCodec(registry).Encode(*editor.scene)):CreateScope<Scene>();
        Save();
        SceneCodec(registry).Save(*next,target);
        ResetRuntime();
        project->SelectScene(relative);
        editor.Replace(std::move(next));
        BindScene();
        Log("Scene created");
    }
    void Play(){
        RICE_BENCHMARK_FUNCTION();
        if(editor.playing)return;
        ResetRuntime();
        editor.Play();
        BindScene();
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
    void PlayAudio(AudioSource& source){ RICE_BENCHMARK_FUNCTION(); source.Play(audio,project->assets.Resolve(source.clip).string());}
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
