#include "Project/Workspace.h"
#include <stb_image.h>
#include <iostream>

using namespace Rice;

namespace {
void Check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
template<class F> void Reject(F action,const char* message) {
    bool rejected=false;
    try { action(); } catch(const std::exception&) { rejected=true; }
    Check(rejected,message);
}
void CheckNewProjectScene(const Scene& scene) {
    std::set<std::string> layers;
    size_t directionalLights=0;
    for(const auto& object:scene.GetObjects()){
        if(object->GetComponent<SceneLayer>()){
            Check(object->isGroup&&!object->GetHierarchyParent(),"Starter layers are root groups");
            Check(layers.insert(object->name).second,"Each starter layer occurs once");
        }
        if(auto* light=object->GetComponent<Light>();light&&light->type==1){
            auto* group=object->GetGroup();
            Check(object->name=="Directional Light"&&object->IsActive()&&light->enabled&&light->intensity>0,
                  "The starter directional light provides light");
            Check(group&&group->IsRequiredLayer()&&group->GetComponent<SceneLayer>()->role==SceneLayer::System,
                  "The starter directional light belongs to System");
            ++directionalLights;
        }
    }
    Check(layers==std::set<std::string>{"System","UI","Game"},"A new project contains the three default layers");
    const auto& settings=scene.settings;
    Check(directionalLights==1&&!settings.directionalLight,"The starter scene has a single directional light source");
    Check(settings.axes==Vector3(1)&&settings.grids==Vector3(0,0,1),"Only XZ grid and all three coordinate axes are visible");
    Check(settings.ground&&settings.groundCollision,"A new project enables the ground plane and its collision");
}
void CheckProjectOpening(const fs::path& output){
    auto root=output/fs::path(std::u8string(u8"한글 프로젝트 ")+fs::path(NewAssetID()).u8string());Workspace workspace(root,true);
    CheckNewProjectScene(*workspace.editor.scene);
    const auto retained=workspace.editor.scene->CreateGameObject("Retained").GetID();workspace.Save();
    auto file=workspace.project->FilePath();
    Check(file.filename()==fs::path(root.filename()).concat(".rice"),"Project file uses its folder name");
    for(const auto* folder:{"Assets","Scripts","Prefabs","Settings","Temp"})
        Check(fs::is_directory(root/folder),"Each project has five working directories");
    Check(Project::ResolveSavedRoot(file)==fs::absolute(root),"Project manifest resolves to its folder");
    Check(Project::ResolveSavedRoot(root)==fs::absolute(root),"Project folder is accepted");
    workspace.Open(file);Check(workspace.editor.scene->Find(retained),"Open accepts a project manifest file");
    CheckNewProjectScene(*workspace.editor.scene);
    auto scene=workspace.editor.scene.get();auto originalRoot=workspace.project->root;
    auto missing=output/(NewAssetID()+"-missing");
    Reject([&]{workspace.Open(missing);},"Missing project rejected");
    Check(!fs::exists(missing),"A failed open never creates a project directory");
    auto invalid=output/(NewAssetID()+"-invalid");fs::create_directories(invalid);AtomicWrite(invalid/"project.rice","not a project");
    Reject([&]{workspace.Open(invalid/"project.rice");},"Invalid manifest rejected");
    Check(!fs::exists(invalid/"Assets"),"Invalid manifest does not construct an asset database");
    AtomicWrite(invalid/"project.rice","RICE_PROJECT 2 0\nActiveScene \"Scenes/Missing.rice\"\n");
    Reject([&]{workspace.Open(invalid);},"Missing active scene rejected before opening");
    Check(!fs::exists(invalid/"Assets"),"Missing scene does not construct an asset database");
    Check(workspace.editor.scene.get()==scene&&workspace.project->root==originalRoot,"Failed open preserves the loaded workspace");
    Reject([&]{Project::ResolveSavedRoot(root/"Assets");},"Asset directories are not projects");
}

void CheckNewProject(const fs::path& output) {
    const auto file=Project::CreateNew(output/(NewAssetID()+"-new-project"));
    Workspace workspace(file,true);
    CheckNewProjectScene(*workspace.editor.scene);
    auto& assets=workspace.project->assets;
    std::map<fs::path,std::string> skyboxes;
    for(const auto& entry:assets.Entries())
        if(entry.path.parent_path()=="Skybox")skyboxes.emplace(entry.path.filename(),entry.id);
    for(const auto* name:{"sf.png","sky.png","night.png"}){
        Check(skyboxes.contains(name),"The project registers each supplied skybox");
        const auto path=ProjectFile::Utf8(assets.Resolve(skyboxes.at(name)));
        int width=0,height=0,channels=0;
        auto* pixels=stbi_load(path.c_str(),&width,&height,&channels,4);
        Check(pixels!=nullptr,"The engine's image decoder reads each supplied skybox");
        stbi_image_free(pixels);
        Check(DetectCubemapLayout(width,height)==CubemapLayout::HorizontalCross,"Each skybox has six square faces");
    }
    const auto sky=skyboxes.at("sky.png"),night=skyboxes.at("night.png");
    auto& initial=workspace.editor.scene->settings;
    Check(initial.skybox&&initial.skyboxAsset==sky,"A new project's saved main scene uses sky.png");
    workspace.Update(0,true);
    Check(workspace.editor.scene->loadedSkyboxAsset.empty()&&!workspace.editor.scene->skyboxTexture,
          "A headless update keeps the default image pending until rendering is available");
    workspace.Play();
    auto& live=workspace.editor.scene->CreateGameObject("Falling box");
    live.transform.position={25,3,30};live.AddComponent<Rigidbody>();
    auto& collider=live.AddComponent<BoxCollider>();
    for(int i=0;i<120;++i)workspace.Update(1.f/60,true);
    Check(std::abs(collider.GetBounds().first.y)<.001f,"A falling collider comes to rest on the starter ground during Play");
    workspace.Stop();
    workspace.editor.scene->settings.skybox=false;
    workspace.editor.scene->settings.skyboxAsset=night;
    workspace.Save();
    workspace.Open(file);
    auto keepsNight=[&]{
        const auto& settings=workspace.editor.scene->settings;
        return !settings.skybox&&settings.skyboxAsset==night;
    };
    Check(keepsNight(),"Reopening preserves the disabled skybox and selected image");
    workspace.NewScene("Duplicate",true);
    Check(keepsNight(),"Duplicating a scene preserves its skybox settings");
    workspace.NewScene("Fresh");
    const auto& fresh=workspace.editor.scene->settings;
    Check(fresh.skybox&&fresh.skyboxAsset==sky,"Creating a blank scene selects sky.png again");
}

struct SpawnOnStart : MonoBehaviour {
    inline static EntityID first=0,reused=0;
    void Start() override {
        auto& scene=GetGameObject().GetScene();
        auto* game=SceneLayers::PlacementLayer(scene);
        game->transform.position={2,4,-6};
        scene.PrewarmPrefab("Reusable",1);
        auto& instance=scene.InstantiatePrefab("Reusable",{3,2,1});
        first=instance.GetID();
        Check(instance.GetGroup()==game&&Vector3(instance.GetWorldMatrix()[3])==Vector3(3,2,1),
              "Script-spawned prefabs bind to Game at the requested world position");
        instance.SetGroup(SceneLayers::Ordered(scene)[1]);
        Check(scene.ReleasePrefab(first),"Script releases pooled instance");
        reused=scene.InstantiatePrefab("Reusable",{4,5,6}).GetID();
        Check(scene.Find(reused)->GetGroup()==game&&Vector3(scene.Find(reused)->GetWorldMatrix()[3])==Vector3(4,5,6),
              "Reacquisition resets old layer membership and retains the new world position");
    }
};

void CheckFbxImport(const fs::path& output) {
    const auto sourceDir=output/(NewAssetID()+"-source");
    fs::create_directories(sourceDir);
    const auto source=sourceDir/fs::path(u8"체스 모델.fbx");
    auto fbx=ReadFile(fs::path(__FILE__).parent_path()/"Fixtures"/"TwoMeshes.fbx");
    const std::string objects="Objects: {",connections="Connections: {";
    fbx.insert(fbx.find(objects)+objects.size(),R"(
    Texture: 6, "Texture::Ivory", "" {
        Type: "TextureVideoClip"
        RelativeFilename: "textures/ivory.tga"
    }
    Video: 7, "Video::Ivory", "Clip" {
        Type: "Clip"
        RelativeFilename: "textures/ivory.tga"
    }
)");
    fbx.insert(fbx.find(connections)+connections.size(),R"(
    C: "OO",7,6
    C: "OP",6,5,"DiffuseColor"
)");
    AtomicWrite(source,fbx);
    std::string image(21,'\0'); // Uncompressed one-pixel RGB TGA.
    image[2]=2;image[12]=1;image[14]=1;image[16]=24;image[20]='\xff';
    AtomicWrite(sourceDir/"textures/ivory.tga",image);
    Workspace w(Project::CreateNew(output/(NewAssetID()+"-import")),true);
    const auto initialAssetCount=w.project->assets.Entries().size();
    const auto existing=w.editor.scene->CreateGameObject("Existing").GetID();
    const auto existingObjects=w.editor.scene->GetObjectCount();
    GameObject* targetLayer=nullptr;
    for(const auto& obj:w.editor.scene->GetObjects())if(obj->name=="Game")targetLayer=obj.get();
    Check(targetLayer!=nullptr,"Import fixture has a target layer");
    w.editor.Select(targetLayer->GetID());
    w.Save();
    const auto imported=w.ImportFbx(source);
    const auto asset=w.project->assets.Root()/source.filename();
    Check(ReadFile(asset)==ReadFile(source)&&w.project->assets.Entries().size()==initialAssetCount+2,
          "FBX import keeps a portable copy of the original");
    Check(w.editor.selected==imported&&w.editor.scene->Find(existing)&&w.editor.scene->GetObjectCount()==existingObjects+3,
          "Import adds a selected model hierarchy to the current scene");
    Check(w.editor.scene->Find(imported)->GetGroup()==targetLayer,"Imported model belongs to the selected layer");
    Check(w.editor.UndoCount()==1&&w.HasUnsavedChanges(),"Import records one undoable edit");
    w.editor.Undo();
    Check(!w.editor.scene->Find(imported)&&w.editor.scene->Find(existing)&&!w.HasUnsavedChanges(),
          "Undo removes only the imported hierarchy");
    w.editor.Redo();
    GameObject* board=nullptr;
    GameObject* piece=nullptr;
    for (auto& obj:w.editor.scene->GetObjects()) {
        if (obj->name=="Board") board=obj.get();
        if (obj->name=="Piece") piece=obj.get();
    }
    Check(board&&piece&&board->GetParent()==w.editor.scene->Find(imported)&&piece->GetParent()==board,
          "Redo restores source objects and parenting");
    auto boardMesh=board->GetComponent<MeshRenderer>();
    auto pieceMesh=piece->GetComponent<MeshRenderer>();
    Check(boardMesh&&pieceMesh&&boardMesh->shape==int(BasicShape::Custom)&&
          boardMesh->custom.indices.size()==6&&pieceMesh->custom.indices.size()==3,
          "The imported board and piece have separate triangle meshes");
    const auto pieceId=piece->GetID();
    auto* material=piece->GetComponent<Material>();
    Check(material&&!material->albedoAsset.empty()&&ReadFile(w.project->assets.Resolve(material->albedoAsset))==image,
          "FBX sidecar textures are copied to the project and linked by asset ID");
    const auto boardBounds=boardMesh->custom.Bounds();
    piece->transform.position.x+=2;
    Check(boardMesh->custom.Bounds()==boardBounds,"Moving the piece leaves the board geometry intact");
    w.Save();
    fs::remove(source);
    fs::remove(sourceDir/"textures/ivory.tga");
    w.Open(w.project->FilePath());
    Check(w.editor.scene->Find(pieceId)->GetComponent<MeshRenderer>()->custom.indices.size()==3&&
          w.editor.scene->Find(pieceId)->GetParent()->name=="Board","Reopening uses the saved mesh without the source file");
    auto second=w.ImportFbx(asset);
    Check(second!=imported&&w.project->assets.Entries().size()==initialAssetCount+2,"An FBX in Assets can be instantiated again");
    w.editor.Undo();
    auto broken=sourceDir/"broken.fbx";
    AtomicWrite(broken,"not an FBX model");
    Reject([&]{w.ImportFbx(broken);},"A damaged FBX cannot be imported");
    Check(!fs::exists(w.project->assets.Root()/broken.filename())&&!w.HasUnsavedChanges(),
          "A failed import preserves both the project and its Assets");
}

void CheckUnsavedChanges(const fs::path& output) {
    auto root=output/(NewAssetID()+"-unsaved");
    Workspace w(Project::CreateNew(root),true);
    Check(!w.HasUnsavedChanges(),"Newly created project is clean");
    auto& object=w.editor.scene->CreateGameObject("Saved");
    auto id=object.GetID();
    const auto editObjects=w.editor.scene->GetObjectCount();
    object.AddComponent<ObjectScript>();
    Check(w.HasUnsavedChanges(),"Direct scene changes are detected without undo history");
    w.Save();
    w.editor.Edit([&](Scene& scene){scene.Find(id)->name="Edited";});
    Check(w.HasUnsavedChanges(),"Undoable edit is dirty");
    w.editor.Undo();
    Check(!w.HasUnsavedChanges(),"Undo to saved contents is clean");
    w.editor.Redo();
    Check(w.HasUnsavedChanges(),"Redo of the edit is dirty again");
    w.editor.Undo();
    w.project->input.Bind("Interact",69);
    Check(w.HasUnsavedChanges(),"An input binding alone is dirty");
    w.project->input.keys.erase("Interact");
    Check(!w.HasUnsavedChanges(),"Restoring the original input bindings clears the change");

    w.Play();
    w.editor.scene->Find(id)->name="Simulation";
    Check(!w.HasUnsavedChanges(),"Playing does not mark the edit document dirty");
    w.Stop();
    w.editor.scene->Find(id)->name="Before Play";
    w.Play();
    w.editor.scene->CreateGameObject("Runtime only");
    w.editor.EditScriptAttachment(id,"ObjectScript",[](ScriptComponentBase& script){script.scriptFile="Scripts/New.cpp";});
    w.Pause();
    Check(w.HasUnsavedChanges(),"Pre-play edits remain dirty while paused");
    w.project->input.Bind("Interact",69);
    fs::rename(root/"Temp",root/"Temp-backup");
    AtomicWrite(root/"Temp","simulate a project save error");
    Reject([&]{w.SaveBeforeClose();},"A failed close-time save reports an error");
    Check(w.HasUnsavedChanges()&&w.editor.playing&&w.editor.paused&&w.editor.scene->GetObjectCount()==editObjects+1,
          "Save failures preserve edits, selection of the project, and paused playback");
    fs::remove(root/"Temp");
    fs::rename(root/"Temp-backup",root/"Temp");
    w.SaveBeforeClose();
    Check(!w.HasUnsavedChanges()&&w.editor.playing&&w.editor.paused,"Successful close-time save preserves playback until close");
    Project disk(root,true);
    auto saved=disk.LoadScene(w.registry);
    Check(saved->GetObjectCount()==editObjects&&saved->Find(id)->name=="Before Play"&&disk.input.keys.at("Interact")==69&&
          saved->Find(id)->GetComponent<ObjectScript>()->scriptFile=="Scripts/New.cpp",
          "Close-time save writes the edit document, persistent script attachment and input bindings");
    w.Stop();
    Check(!w.HasUnsavedChanges(),"Returning from play to the saved edit document is clean");
    w.NewScene("Second",true);
    Check(!w.HasUnsavedChanges(),"Creating and selecting a saved scene resets the comparison");
    w.project->input.Bind("Uncommitted",85);
    auto inactive=w.project->LoadScene(w.registry,"main.rice");
    inactive->Find(id)->name="Inactive scene edit";
    w.project->SaveInactiveScene("main.rice",*inactive,w.registry);
    Project afterInactive(root,true);
    Check(afterInactive.ActiveScene()==w.project->ActiveScene()&&!afterInactive.input.keys.contains("Uncommitted")&&
          afterInactive.LoadScene(w.registry,"main.rice")->Find(id)->name=="Inactive scene edit"&&w.HasUnsavedChanges(),
          "Saving an inactive scene leaves the current document and unsaved bindings untouched");
    w.editor.scene->CreateGameObject("Second only");
    w.SwitchScene("main.rice");
    Check(!w.HasUnsavedChanges(),"Switching scenes establishes the incoming saved state");
    w.editor.scene->CreateGameObject("Discard on open");
    w.Open(root);
    Check(!w.HasUnsavedChanges()&&w.editor.scene->GetObjectCount()==editObjects,"Opening restores the saved document");
}

struct SimulationProbe : Component {
    int updates=0,lateUpdates=0;
    void Update(float) override { ++updates; }
    void LateUpdate(float) override { ++lateUpdates; }
};
void CheckPlayback(const fs::path& output){
    Workspace workspace(output/(NewAssetID()+"-playback"),true,[](ComponentRegistry& registry){
        registry.Register<SimulationProbe>("SimulationProbe",[](auto&,auto&){});
    });
    auto& editor=workspace.editor;
    editor.scene->settings.ground=false; // This fixture supplies its own floor below Y=0.
    auto& moving=editor.scene->CreateGameObject("Kinematic");auto movingID=moving.GetID();
    moving.transform.position={-10,5,0};auto& body=moving.AddComponent<Rigidbody>();body.kinematic=true;body.velocity={3,0,0};
    moving.AddComponent<BoxCollider>();moving.AddComponent<SimulationProbe>();
    auto& animation=moving.AddComponent<SpriteAnimator>();animation.frames=4;animation.columns=4;animation.fps=12;
    auto& falling=editor.scene->CreateGameObject("Dynamic");auto fallingID=falling.GetID();falling.transform.position={0,3,0};
    falling.AddComponent<Rigidbody>();falling.AddComponent<BoxCollider>();
    auto& floor=editor.scene->CreateGameObject("Floor");floor.transform.position={0,-1,0};floor.transform.localScale={20,1,20};floor.AddComponent<BoxCollider>();
    auto initial=SceneCodec(workspace.registry).Encode(*editor.scene);
    workspace.Pause();workspace.Resume();workspace.Update(.1f,true);
    Check(!editor.playing&&!editor.paused&&SceneCodec(workspace.registry).Encode(*editor.scene)==initial,"Edit mode does not simulate");
    workspace.Play();workspace.Update(1.f/60,true);
    AudioSource sound;sound.loop=true;
    sound.clip=workspace.project->assets.Import(fs::path(__FILE__).parent_path().parent_path()/"Sandbox/Assets/tone.wav");
    workspace.project->assets.Rename(sound.clip,fs::path(u8"음악/효과음.wav"));
    workspace.PlayAudio(sound);
    FMOD::System* audioSystem=nullptr;AudioSystem::Check(sound.channel->getSystemObject(&audioSystem));
    FMOD::ChannelGroup* master=nullptr;AudioSystem::Check(audioSystem->getMasterChannelGroup(&master));
    auto audioPaused=[&]{bool paused=false;AudioSystem::Check(master->getPaused(&paused));return paused;};
    auto* live=editor.scene->Find(movingID);auto* dynamic=editor.scene->Find(fallingID);
    Check(std::abs(live->transform.position.x+9.95f)<.0001f&&live->transform.position.y==5,"Kinematic velocity runs during Play without gravity");
    Check(dynamic->transform.position.y<3,"Dynamic gravity runs during Play");
    workspace.Pause();workspace.Pause();Check(audioPaused(),"Pause pauses audio mixer");
    auto position=live->transform.position;auto dynamicPosition=dynamic->transform.position;
    auto velocity=dynamic->GetComponent<Rigidbody>()->velocity;auto clock=live->GetComponent<SpriteAnimator>()->clock;
    auto* probe=live->GetComponent<SimulationProbe>();auto updates=probe->updates,late=probe->lateUpdates;
    for(int i=0;i<120;++i)workspace.Update(.1f,true);
    Check(editor.playing&&editor.paused&&live->transform.position==position&&dynamic->transform.position==dynamicPosition&&dynamic->GetComponent<Rigidbody>()->velocity==velocity,"Pause freezes dynamic and kinematic physics");
    Check(probe->updates==updates&&probe->lateUpdates==late&&live->GetComponent<SpriteAnimator>()->clock==clock,"Pause freezes scripts and animation");
    workspace.Resume();Check(!audioPaused(),"Resume unpauses audio mixer");workspace.Update(1.f/60,true);
    Check(!editor.paused&&editor.scene->Find(movingID)==live&&std::abs(live->transform.position.x-position.x-.05f)<.0001f,"Resume continues same scene without accumulating paused time");
    for(int i=0;i<240;++i)workspace.Update(1.f/60,true);
    Check(std::abs(dynamic->transform.position.y)<.01f,"Dynamic body collides with floor");
    workspace.Pause();workspace.Stop();Check(!audioPaused()&&!sound.IsPlaying(),"Stop clears mixer pause and stops audio");
    Check(!editor.playing&&!editor.paused&&SceneCodec(workspace.registry).Encode(*editor.scene)==initial,"Stop from paused restores edit state");
    workspace.Update(.1f,true);Check(SceneCodec(workspace.registry).Encode(*editor.scene)==initial,"Stop halts physics");
    workspace.Play();workspace.Update(1.f/60,true);
    Check(!editor.paused&&editor.scene->Find(movingID)->transform.position.x>-10,"Replay after paused Stop runs normally");
    workspace.Stop();
}

void CheckSceneSettings(const fs::path& output){
    Workspace workspace(output/(NewAssetID()+"-settings"),true);auto& scene=*workspace.editor.scene;
    auto& settings=scene.settings;settings.skybox=true;settings.skyboxAsset="sky-id";settings.directionalLight=true;
    settings.lightColor={.3f,.5f,.8f,1};settings.lightIntensity=2.5f;settings.lightDirection={1,-2,3};settings.ground=true;settings.groundCollision=false;
    settings.axes={1,0,1};settings.grids={0,1,0};
    SceneCodec codec(workspace.registry);auto saved=codec.Encode(scene);auto loaded=codec.Decode(saved);
    Check(codec.Encode(*loaded)==saved,"All scene settings roundtrip");
    auto exported=codec.Encode(scene,false);Check(exported.find("EditorGrid")==std::string::npos,"Export omits editor grid settings");
    auto runtime=codec.Decode(exported);Check(runtime->settings.ground&&!runtime->settings.groundCollision&&runtime->settings.skyboxAsset=="sky-id","Runtime scene retains environment settings");
    auto legacy=codec.Decode("RICE_SCENE 4\nEndScene\n");Check(!legacy->settings.ground&&!legacy->settings.directionalLight,"Legacy scenes retain original environment");
    workspace.Save();workspace.NewScene("Environment Copy",true);Check(codec.Encode(*workspace.editor.scene)==saved,"Duplicate scene retains settings");
    workspace.NewScene("Empty",false);Check(!workspace.editor.scene->settings.ground,"New scene settings are independent");
    workspace.SwitchScene("Scenes/Environment Copy.rice");Check(codec.Encode(*workspace.editor.scene)==saved,"Scene switch restores settings");
    workspace.editor.Edit([](Scene& value){value.settings.ground=false;});workspace.editor.Undo();Check(workspace.editor.scene->settings.ground,"Scene settings support Undo");
    workspace.editor.Redo();Check(!workspace.editor.scene->settings.ground,"Scene settings support Redo");
    Scene physicsScene;physicsScene.settings.ground=true;PhysicsSystem physics;
    auto& body=physicsScene.CreateGameObject("Falling");body.transform.position={10000,3,10000};body.AddComponent<Rigidbody>();body.AddComponent<BoxCollider>();
    for(int i=0;i<180;++i)physics.Update(physicsScene,1.f/60);
    Check(std::abs(body.GetComponent<BoxCollider>()->GetBounds().first.y)<.001f,"Infinite ground stops collider at y=0 far from origin");
    physicsScene.settings.groundCollision=false;for(int i=0;i<60;++i)physics.Update(physicsScene,1.f/60);
    Check(body.transform.position.y<0,"Ground collision can be disabled independently");
    physicsScene.settings.groundCollision=true;physicsScene.settings.ground=false;body.transform.position.y=3;body.GetComponent<Rigidbody>()->velocity=Vector3(0);
    for(int i=0;i<60;++i)physics.Update(physicsScene,1.f/60);
    Check(body.transform.position.y<0,"Hidden ground does not collide");
}

void CheckNamedSceneOnly(const fs::path& output) {
    auto root=output/(NewAssetID()+"-named-scene-only");
    ComponentRegistry registry;
    Scene scene;
    auto& object=scene.CreateGameObject("Named scene object");
    object.transform.position={7,8,9};
    object.transform.localScale={3,4,5};
    object.AddComponent<Light>();
    auto objectID=object.GetID();
    auto& piece=scene.CreateGameObject("Child piece");
    piece.SetParent(&object);
    piece.transform.position={1,2,3};
    piece.AddComponent<Rigidbody>();
    const auto pieceID=piece.GetID();
    const auto pieceWorld=piece.GetWorldMatrix();
    SceneCodec(registry).Save(scene,root/"Scenes"/"Second.rice");
    AtomicWrite(root/"project.rice","RICE_PROJECT 2 1\n\"Jump\" 32\nActiveScene \"Scenes/Second.rice\"\n");
    Check(!fs::exists(root/"main.rice"),"Named-scene fixture must have no main scene");
    {
        Workspace workspace(root,true);
        Check(workspace.project->ActiveScene()=="Scenes/Second.rice"&&workspace.editor.scene->GetObjectCount()==5,
              "Version 2 project opens its named scene without main.rice");
        Check(workspace.editor.scene->Find(objectID)&&workspace.editor.scene->Find(objectID)->transform.position==Vector3(7,8,9),
              "Named scene loads original objects and properties");
        auto* rootObject=workspace.editor.scene->Find(objectID);
        auto* layer=rootObject->GetGroup();
        auto* child=workspace.editor.scene->Find(pieceID);
        Check(layer&&layer->name=="Game"&&layer->GetComponent<SceneLayer>()&&child&&
              child->GetParent()==rootObject&&!child->GetGroup()&&child->GetWorldMatrix()==pieceWorld&&
              child->GetComponent<Rigidbody>()&&rootObject->GetComponent<Light>(),
              "Legacy models acquire a layer without changing authored hierarchy, transforms or components");
        auto secondLoad=workspace.project->LoadScene(workspace.registry);
        Check(secondLoad->Find(objectID)->GetGroup()->GetID()==layer->GetID(),"Layers have stable IDs before the first save");
        layer->name="Board pieces";
        workspace.editor.scene->CreateGameObject("Loose game object");
        workspace.Save();
    }
    Workspace reopened(root,true);
    Check(!fs::exists(root/"main.rice")&&reopened.project->ActiveScene()=="Scenes/Second.rice"&&
          reopened.project->SceneFiles()==std::vector<fs::path>{"Scenes/Second.rice"},
          "Reopening a named-scene-only project does not create or require main.rice");
    Check(reopened.editor.scene->GetObjectCount()==6&&reopened.editor.scene->Find(objectID)->GetGroup()->name=="Board pieces"&&
          reopened.editor.scene->Find(pieceID)->GetWorldMatrix()==pieceWorld,
          "Reopening reuses renamed layers and preserves the model transform");
    for(const auto& obj:reopened.editor.scene->GetObjects())
        Check(obj->GetComponent<SceneLayer>()||obj->GetHierarchyParent(),"Previously loose objects in a layered scene also receive membership");
}

void CheckLayerPlacement() {
    Scene scene;
    auto& board=scene.CreateGameObject("Game");
    scene.CreateGameObject("System Layer");
    SceneLayers::Migrate(scene);
    auto* layer=board.GetGroup();
    Check(layer&&layer->name=="Game (1)"&&board.name=="Game","Generated game layers avoid authored object names");
    layer->transform.position={5,8,-1};
    layer->transform.rotation={0,45,20};
    layer->transform.localScale={2,3,1};
    auto& piece=scene.CreateGameObject("Piece");
    piece.transform.position={2,3,4};
    piece.transform.localScale={1,0,3};
    const auto world=piece.GetWorldMatrix();
    SceneLayers::Migrate(scene);
    Check(piece.GetGroup()==layer&&piece.transform.localScale==Vector3(1,0,3),"Migration retains singular authored transforms");
    const auto grouped=piece.GetWorldMatrix();
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)
        Check(std::abs(grouped[c][r]-world[c][r])<.00001f,"A transformed destination layer preserves the world matrix");
    scene.EachComponent<SceneLayer>([](SceneLayer& marker){marker.GetGameObject().SetActive(false);});
    auto& added=scene.CreateGameObject("New piece");
    SceneLayers::PlaceRoot(added,layer);
    Check(added.GetGroup()&&added.IsActive()&&!layer->IsActive(),"New content remains visible when existing layers are inactive");
}

void CheckLayerObjectLimit() {
    Scene scene;
    for(size_t i=0;i<SceneCodec::MaxObjects;++i)scene.CreateGameObject("Legacy object",i+1);
    SceneLayers::Migrate(scene);
    ComponentRegistry registry;
    SceneCodec codec(registry);
    auto restored=codec.Decode(codec.Encode(scene));
    Check(restored->GetObjectCount()==SceneCodec::MaxObjects+SceneCodec::DefaultLayerCount&&
          !SceneCodec::FitsObjectLimit(*restored,1),"A full legacy scene round-trips with three layers and the same content limit");
}

void CheckLegacyLayerRoles() {
    Scene original;
    auto legacyLayer=[&](const char* name, EntityID id)->GameObject& {
        auto& layer=original.CreateGameObject(name,id);
        layer.isGroup=true;
        layer.AddComponent<SceneLayer>();
        return layer;
    };
    auto& oldSystem=legacyLayer("System Layer",35);
    auto& oldUI=legacyLayer("UI",36);
    legacyLayer("Layer 0",37);
    legacyLayer("Layer 2",38);
    auto& sun=original.CreateGameObject("Directional Light",40);
    sun.SetGroup(&oldSystem,false);
    sun.AddComponent<Light>().type=1;
    auto& text=original.CreateGameObject("Custom",39);
    text.SetGroup(&oldUI,false);
    text.AddComponent<UIText>();
    text.groupOffset=glm::translate(Matrix4(1),Vector3(3,2,1));
    auto& board=original.CreateGameObject("ChessBoard",1);
    board.isGroup=true;
    board.SetGroup(&oldUI,false);
    board.transform.position={2,3,4};
    auto& rook=original.CreateGameObject("Rook1",2);
    rook.SetParent(&board);
    auto rookWorld=rook.GetWorldMatrix();
    auto textWorld=text.GetWorldMatrix();

    ComponentRegistry registry;
    SceneCodec codec(registry);
    auto encoded=codec.Encode(original);
    const std::string roleLine="\"Role\" -1\n";
    for(auto p=encoded.find(roleLine);p!=std::string::npos;p=encoded.find(roleLine))
        encoded.erase(p,roleLine.size());
    auto boardRecord=encoded.find("Object 1 0 \"ChessBoard\"");
    Check(boardRecord!=std::string::npos,"Legacy ChessBoard fixture exists");
    encoded.insert(encoded.find("EndObject",boardRecord),"Component \"Scene Layer\"\n\"Enabled\" 1\nEndComponent\n");
    auto migrated=codec.Decode(encoded);
    SceneLayers::Migrate(*migrated);
    auto layers=SceneLayers::Ordered(*migrated);
    Check(layers.size()==3&&layers[0]->GetID()==35&&layers[1]->GetID()==36&&layers[2]->GetID()==38&&
          layers[0]->name=="System"&&layers[1]->name=="UI"&&layers[2]->name=="Game",
          "Legacy layer roots retain their IDs and acquire the three ordered roles");
    Check(layers[0]->IsRequiredLayer()&&layers[1]->IsRequiredLayer()&&!layers[2]->IsRequiredLayer(),
          "Only System and UI are mandatory");
    auto* model=migrated->Find(1);
    Check(!model->GetComponent<SceneLayer>()&&!model->GetComponent<MeshRenderer>()&&model->GetGroup()==layers[2]&&
          migrated->Find(2)->GetParent()==model&&migrated->Find(2)->GetWorldMatrix()==rookWorld,
          "A nested legacy layer becomes an invisible Game object and retains its model children");
    Check(migrated->Find(39)->GetGroup()==layers[2]&&migrated->Find(39)->GetWorldMatrix()==textWorld&&
          migrated->Find(40)->GetGroup()==layers[2],
          "All pre-existing content migrates to Game, including legacy UI and light objects");
    migrated->Find(39)->SetGroup(layers[1]);
    const auto canonical=codec.Encode(*migrated);
    auto reopened=codec.Decode(canonical);
    SceneLayers::Migrate(*reopened);
    Check(codec.Encode(*reopened)==canonical,"Reopening a migrated scene preserves deliberate UI membership");

    EditorSession editor(registry);
    editor.scene=std::move(reopened);
    editor.Select(35);
    editor.Select(36,true);
    editor.DeleteSelection();
    Check(editor.Snapshot()==canonical,"Delete leaves a protected-only selection intact");
    editor.Select(38,true);
    editor.DeleteSelection();
    Check(editor.scene->Find(35)&&editor.scene->Find(36)&&!editor.scene->Find(38),
          "Mixed selection deletes only editable layers and their content");
    SceneLayers::Migrate(*editor.scene);
    Check(SceneLayers::Ordered(*editor.scene).size()==2,"Reopening respects a deleted Game layer");
    auto& fresh=editor.scene->CreateGameObject();
    SceneLayers::PlaceRoot(fresh);
    Check(fresh.GetGroup()&&fresh.GetGroup()->name=="Game","Creating new content restores a game placement layer on demand");
}

void CheckScenesAndPrefabs(const fs::path& output) {
    auto root=output/NewAssetID();
    fs::create_directories(root);
    AtomicWrite(root/"project.rice","RICE_PROJECT 1 1\n\"Jump\" 32\n");
    ComponentRegistry registry;
    Scene original;
    original.CreateGameObject("Original").AddComponent<MeshRenderer>();
    SceneCodec(registry).Save(original,root/"main.rice");
    auto registerTypes=[](ComponentRegistry& types) {
        types.Register<SpawnOnStart>("SpawnOnStart",[](auto&,auto&){});
    };
    Workspace workspace(root,true,registerTypes);
    auto& editor=workspace.editor;
    Check(workspace.project->ActiveScene()=="main.rice","Legacy project opens main scene");
    auto originalID=editor.scene->GetObjects().front()->GetID();
    editor.scene->Find(originalID)->transform.position={1,2,3};
    workspace.NewScene("Second");
    Check(editor.scene->GetObjectCount()==3&&workspace.project->ActiveScene()=="Scenes/Second.rice","Create and select a new scene with its three empty layers");
    auto saved=Project(root,true).LoadScene(workspace.registry,"main.rice");
    Check(saved->Find(originalID)->transform.position==Vector3(1,2,3),"Scene transition saves outgoing edits");
    editor.scene->CreateGameObject("Second object");
    workspace.SwitchScene("main.rice");
    Check(editor.scene->Find(originalID)&&editor.scene->GetObjectCount()==4,"Switch restores independent scene");
    Check(Project(root,true).LoadScene(workspace.registry,"Scenes/Second.rice")->GetObjectCount()==4,"Second scene saved on switch");
    size_t projectFiles=0;
    for(const auto& f:fs::recursive_directory_iterator(root))
        if(f.is_regular_file()&&f.path().extension()==".rice")++projectFiles;
    Check(projectFiles==1&&fs::exists(root/"Temp/Legacy/main.rice.legacy"),"Saving embeds every scene and preserves legacy backups");
    workspace.NewScene("Copy",true);
    Check(editor.scene->Find(originalID)&&workspace.project->SceneFiles().size()==3,"Duplicate scene and discover all scenes");
    {
        Workspace reopened(root,true,registerTypes);
        Check(reopened.project->ActiveScene()=="Scenes/Copy.rice"&&reopened.editor.scene->Find(originalID),"Selected scene persists across reopen");
    }
    auto current=editor.scene->GetInstanceID();
    Reject([&]{workspace.NewScene("../escape");},"Scene name traversal rejected");
    Reject([&]{workspace.NewScene("Second");},"Duplicate scene name rejected");
    Reject([&]{workspace.SwitchScene("Scenes/Missing.rice");},"Missing scene rejected");
    Check(editor.scene->GetInstanceID()==current,"Failed switch keeps live scene");

    workspace.SavePrefab(originalID,"Reusable");
    auto beforePrefab=editor.Snapshot();
    EntityID placedID=0;
    editor.Edit([&](Scene&){placedID=workspace.InstantiatePrefab("Reusable",{8,0,0}).GetID();});
    Check(editor.scene->GetObjectCount()==5&&editor.scene->Find(placedID)->GetGroup()==editor.scene->Find(originalID)->GetGroup(),
          "Editing places a persistent prefab instance in the default layer");
    auto afterPrefab=editor.Snapshot();
    editor.Undo();
    workspace.BindScene();
    Check(editor.Snapshot()==beforePrefab&&!editor.scene->Find(placedID),
          "Undo removes the edited prefab placement and restores the preceding scene");
    editor.Redo();
    workspace.BindScene();
    Check(editor.Snapshot()==afterPrefab&&editor.scene->Find(placedID)&&
          editor.scene->Find(placedID)->transform.position==Vector3(8,0,0),
          "Redo restores the prefab identity, hierarchy and position");
    EntityID recreatedID=0;
    auto* selectedUI=SceneLayers::Ordered(*editor.scene)[1];
    editor.Select(selectedUI->GetID());
    editor.Edit([&](Scene& restored){recreatedID=restored.InstantiatePrefab("Reusable",{9,0,0}).GetID();});
    auto* recreated=editor.scene->Find(recreatedID);
    Check(recreated&&recreatedID!=placedID&&!recreated->IsPooled()&&recreated->GetGroup()==selectedUI&&recreated->transform.position==Vector3(9,0,0)&&
          editor.scene->GetObjectCount()==6,
          "Rebound scene callbacks place a fresh edit instance in the selected UI layer");
    editor.Undo();
    workspace.BindScene();
    Check(editor.Snapshot()==afterPrefab,"Recreated prefab placement has its own undo record");
    editor.scene->CreateGameObject("Spawner").AddComponent<SpawnOnStart>();
    workspace.Save();
    const auto editSnapshot=SceneCodec(workspace.registry).Encode(*editor.scene);
    {
        BenchmarkSession session(root/"benchmark.tsv");
        for(int i=0;i<3;++i){BenchmarkFrame frame;Check(Benchmark::CurrentFrame()==0,"Editing remains frame zero");}
        Check(Benchmark::Schedule(1,1,1),"Schedule first play frame while editing");
        workspace.Play();
        {
            BenchmarkFrame frame;
            Check(Benchmark::CurrentFrame()==1,"First played frame starts at one");
            workspace.Update(.016f,true);
            Check(SpawnOnStart::first&&SpawnOnStart::first==SpawnOnStart::reused,"Normal Start callback reuses a pooled object");
            auto* instance=editor.scene->Find(SpawnOnStart::reused);
            Check(instance&&instance->IsPooled()&&instance->transform.position==Vector3(4,5,6),"Script spawn restores state at requested location");
            Reject([&]{workspace.SwitchScene("main.rice");},"Scene changes during Play rejected");
        }
        workspace.Stop();
        Check(!Benchmark::IsPlaying()&&Benchmark::CurrentFrame()==0,"Stop resets benchmark frame");
        Check(SceneCodec(workspace.registry).Encode(*editor.scene)==editSnapshot,"Stop restores edit instances and removes runtime pool");
        workspace.Play();
        {BenchmarkFrame frame;Check(Benchmark::CurrentFrame()==1,"Replay restarts at frame one");}
        workspace.Stop();
    }
    workspace.SwitchScene("main.rice");
    Check(editor.scene->GetObjectCount()==4,"Pool cannot leak into another scene");
    Check(!workspace.ReleasePrefab(SpawnOnStart::reused),"Old pooled handle cannot be released into another scene");

    workspace.SwitchScene("Scenes/Second.rice");
    auto binaries=root/"Install"/"Engine"/"Runtime";
    fs::create_directories(binaries);
    AtomicWrite(binaries/"RicePlayer.exe","export-test-placeholder");
    AtomicWrite(binaries/"RiceGameHost.exe","game-host-test-placeholder");
    AtomicWrite(binaries/"fmod.dll","dependency");
    AtomicWrite(binaries/"avcodec-62.dll","licensed dependency");
    AtomicWrite(binaries/"editor.dll","editor-only dependency");
    AtomicWrite(binaries/"RicePlayer.dlls","fmod.dll\navcodec-62.dll\n");
    AtomicWrite(binaries.parent_path()/"ThirdParty/FFmpeg/LICENSE.txt","FFmpeg notice");
    auto exported=output/(NewAssetID()+"-export");
    workspace.project->Export(*editor.scene,workspace.registry,binaries,exported);
    auto build=exported/fs::path(root.filename()).concat(".exe");
    Check(IsGameBundle(build)&&std::distance(fs::directory_iterator(exported),fs::directory_iterator{})==1,"The build consists of a single game executable");
    auto unpacked=output/(NewAssetID()+"-unpacked");
    fs::create_directories(unpacked);
    auto moved=unpacked/fs::path(u8"다른 이름.exe");
    fs::rename(build,moved);
    auto contents=unpacked/"Contents";fs::create_directories(contents);
    ExtractGameBundle(moved,contents);
    const auto shippedSky=contents/"Project"/"Assets"/"Skybox"/"sky.png";
    Check(fs::is_regular_file(shippedSky)&&ReadFile(shippedSky)==ReadFile(workspace.project->assets.Resolve(editor.scene->settings.skyboxAsset)),
          "The relocated game carries the selected default skybox");
    Check(fs::exists(contents/"fmod.dll")&&fs::exists(contents/"RicePlayer.exe"),"Relocated build contains the runtime");
    Check(!fs::exists(contents/"editor.dll")&&fs::exists(contents/"Licenses/FFmpeg/LICENSE.txt"),"Export selects the player's DLLs and includes their notices");
    Project exportedProject(contents/"Project",true);
    Check(exportedProject.ActiveScene()=="Scenes/Second.rice"&&exportedProject.SceneFiles().size()==3,"Export retains scenes and active scene");
    Check(fs::is_regular_file(contents/"Project"/"Prefabs"/"Reusable.prefab"),"Export contains prefab assets");
    Check(exportedProject.LoadScene(workspace.registry)->GetObjectCount()==4,"Exported active scene and layers load");
}
}

int main(int argc,char** argv) {
    try {
        Check(argc==2,"Expected test output directory");
        Debug::Init();
        CheckProjectOpening(argv[1]);
        CheckNewProject(argv[1]);
        CheckFbxImport(argv[1]);
        CheckUnsavedChanges(argv[1]);
        CheckSceneSettings(argv[1]);
        CheckPlayback(argv[1]);
        CheckNamedSceneOnly(argv[1]);
        CheckLayerPlacement();
        CheckLegacyLayerRoles();
        CheckLayerObjectLimit();
        CheckScenesAndPrefabs(argv[1]);
        std::cout<<"Workspace scene transitions and pooled prefab integration passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
