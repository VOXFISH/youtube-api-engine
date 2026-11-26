#include "Project/Workspace.h"
#include <iostream>
using namespace Rice;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Events:Component {int enter=0,stay=0,exit=0;void OnTriggerEnter(EntityID)override{++enter;}void OnTriggerStay(EntityID)override{++stay;}void OnTriggerExit(EntityID)override{++exit;}};
int main(int argc,char** argv){try{
    ComponentRegistry registry;registry.Register<Events>("Events",[](auto&,auto&){});SceneCodec codec(registry);EditorSession editor(registry);
    auto& root=editor.scene->CreateGameObject("Root");auto rootID=root.GetID();root.transform.position={3,0,0};
    auto& child=editor.scene->CreateGameObject("Child");auto childID=child.GetID();child.SetParent(&root);child.transform.position={1,0,0};
    child.AddComponent<SpriteRenderer>().asset="stable-id";child.AddComponent<AudioSource>().clip="audio-id";
    Check(child.GetWorldMatrix()[3].x==4,"Hierarchy world transform");child.TranslateWorld({1,0,0});Check(child.transform.position.x==2,"World translation");
    bool rejected=false;try{root.SetParent(&child);}catch(...){rejected=true;}Check(rejected,"Hierarchy cycle rejected");
    root.AddComponent<MeshRenderer>().color={.1f,.2f,.3f,1};
    root.AddComponent<Light>().intensity=2;
    root.AddComponent<Rigidbody>().mass=3;
    root.AddComponent<BoxCollider>().isTrigger=true;
    root.AddComponent<SpriteAnimator>().columns=4;
    root.AddComponent<UIText>().text="quoted \"text\" and newline\n";
    root.AddComponent<UIButton>().action="Quit";
    root.AddComponent<PlayerController>().speed=6;
    auto encoded=codec.Encode(*editor.scene);auto restored=codec.Decode(encoded);Check(codec.Encode(*restored)==encoded,"Roundtrip all properties and references");
    const std::string physicsHeader="Component \"Physics\"";
    auto physicsAt=encoded.find(physicsHeader);
    Check(physicsAt!=std::string::npos,"Physics uses the new component name");
    auto oldPhysicsScene=encoded;
    oldPhysicsScene.replace(physicsAt,physicsHeader.size(),"Component \"Rigidbody\"");
    Check(codec.Encode(*codec.Decode(oldPhysicsScene))==encoded,"Opening and resaving a Rigidbody scene preserves its settings as Physics");
    auto oldMeshScene=encoded;
    auto shapeLine=oldMeshScene.find("\"Shape\" 0\n");
    Check(shapeLine!=std::string::npos,"Shape field exists in new scenes");
    oldMeshScene.erase(shapeLine,std::string("\"Shape\" 0\n").size());
    auto oldRestored=codec.Decode(oldMeshScene);
    Check(oldRestored->Find(rootID)->GetComponent<MeshRenderer>()->shape==0,"Legacy meshes default to Cube");
    Check(codec.Encode(*oldRestored)==encoded,"Legacy mesh preserves all scene data");
    for(int version:{1,2,3}){
        std::string header="RICE_SCENE "+std::to_string(version)+"\nObject 900 0 \"Old cube\" 1 0 0 0 0 0 0 1 1 1 ";
        if(version>=2){
            header+="0 0 0 ";
            for(int matrix=0;matrix<4;++matrix)
                header+="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 ";
        }
        auto oldCube=codec.Decode(header+"\nComponent \"MeshRenderer\"\n\"Enabled\" 1\n\"Color\" .1 .2 .3 1\nEndComponent\nEndObject\nEndScene\n");
        Check(oldCube->Find(900)->GetComponent<MeshRenderer>()->shape==0,"All legacy scene versions load cubes without Shape");
    }
    rejected=false;try{codec.Decode(encoded.substr(0,encoded.size()-12));}catch(...){rejected=true;}Check(rejected,"Truncated file rejected");
    auto& copy=codec.Duplicate(*editor.scene,childID);Check(copy.GetID()!=childID&&copy.GetParent()==&root&&copy.GetComponent<SpriteRenderer>()->asset=="stable-id","Duplicate preserves asset and parent");
    editor.Edit([&](Scene& s){s.Find(childID)->name="Changed";});editor.Undo();Check(editor.scene->Find(childID)->name=="Child","Undo");editor.Redo();Check(editor.scene->Find(childID)->name=="Changed","Redo");
    auto prePlay=codec.Encode(*editor.scene);editor.Play();editor.scene->Find(rootID)->transform.position={100,0,0};editor.Stop();Check(codec.Encode(*editor.scene)==prePlay,"Play restores edit scene");
    editor.scene->Destroy(rootID);Check(!editor.scene->Find(childID),"Recursive deferred deletion hides child");editor.scene->Flush();Check(editor.scene->GetObjectCount()==0,"Recursive destruction");
    Scene physicsScene;auto& floor=physicsScene.CreateGameObject("Floor");floor.transform.position={0,-1,0};floor.transform.localScale={10,1,10};floor.AddComponent<BoxCollider>();
    auto& falling=physicsScene.CreateGameObject("Falling");falling.transform.position={0,3,0};falling.AddComponent<BoxCollider>();falling.AddComponent<Rigidbody>();PhysicsSystem physics;
    for(int i=0;i<240;++i)physics.Update(physicsScene,1.f/60);Check(std::abs(falling.transform.position.y)<.01f,"Rigidbody rests on floor");
    {
        Scene scene;PhysicsSystem system;
        auto& platform=scene.CreateGameObject("Kinematic pusher");auto& body=platform.AddComponent<Rigidbody>();
        body.kinematic=true;body.velocity={2,0,0};platform.AddComponent<BoxCollider>();
        auto& pushed=scene.CreateGameObject("Dynamic body");pushed.transform.position={1,0,0};auto& pushedBody=pushed.AddComponent<Rigidbody>();pushedBody.useGravity=false;pushed.AddComponent<BoxCollider>();
        system.Update(scene,1.f/60);
        Check(std::abs(platform.transform.position.x-2.f/60)<.0001f&&platform.transform.position.y==0,"Kinematic integrates velocity without gravity or collision displacement");
        Check(pushed.transform.position.x>1&&pushedBody.velocity.x>0,"Moving kinematic participates in dynamic collision response");
        body.enabled=false;auto position=platform.transform.position;system.Update(scene,1.f/60);
        Check(platform.transform.position==position,"Disabled Rigidbody does not move");
    }
    auto& trigger=physicsScene.CreateGameObject("Trigger");trigger.AddComponent<BoxCollider>().isTrigger=true;auto& events=trigger.AddComponent<Events>();
    physics.Update(physicsScene,1.f/60);Check(events.enter>0,"Trigger enter");physics.Update(physicsScene,1.f/60);Check(events.stay>0,"Trigger stay");trigger.transform.position={100,0,0};physics.Update(physicsScene,1.f/60);Check(events.exit>0,"Trigger exit");
    auto& animator=trigger.AddComponent<SpriteAnimator>();animator.columns=4;animator.frames=4;animator.fps=4;animator.Update(.5f);Check(animator.frame==2,"Animation frame");animator.loop=false;animator.Update(4);Check(animator.frame==3,"Nonloop animation");
    fs::path testRoot=argc>1?fs::path(argv[1]):fs::path("TestData");testRoot/=NewAssetID();fs::create_directories(testRoot);AtomicWrite(testRoot/"sample.txt","asset");
    AssetDatabase assets(testRoot/"Assets");auto id=assets.Import(testRoot/"sample.txt");assets.Rename(id,"Nested/renamed.txt");Check(ReadFile(assets.Resolve(id))=="asset","Stable asset rename");
    AssetDatabase reopened(testRoot/"Assets");Check(reopened.Resolve(id).filename()=="renamed.txt","Asset ID persists");
    rejected=false;try{assets.Rename(id,"../escape.txt");}catch(...){rejected=true;}Check(rejected,"Asset path containment");
    codec.Save(physicsScene,testRoot/"scene.rice");Check(codec.Load(testRoot/"scene.rice")->GetObjectCount()==3,"Scene disk roundtrip");
    if(argc>2){AudioSystem audio(true);AudioSource source;source.Play(audio,argv[2]);Check(source.IsPlaying(),"FMOD channel started");source.Pause(true);source.Pause(false);source.volume=.2f;source.Sync();audio.Update();source.Stop();Check(!source.IsPlaying(),"FMOD channel stopped");}
    std::cout<<"Engine integration tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
