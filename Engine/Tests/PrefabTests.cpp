#include "Project/Prefab.h"
#include <iostream>

using namespace Rice;

namespace {
void Check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F>void Reject(F fn,const char* message) {
    bool failed=false;try{fn();}catch(const std::exception&){failed=true;}Check(failed,message);
}
bool Near(const Matrix4& a,const Matrix4& b) {
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>.003f)return false;
    return true;
}
struct Probe:Component {
    inline static int awakened=0,started=0,enabledCalls=0,disabledCalls=0,acquired=0,released=0,destroyed=0,enters=0;
    inline static bool failAcquire=false,failDisable=false;
    inline static std::function<void(Probe&)> acquireAction;
    int value=7,transient=0;
    void Awake() override { ++awakened; }
    void Start() override { ++started; }
    void OnEnable() override { ++enabledCalls; }
    void OnDisable() override { ++disabledCalls;if(failDisable)throw std::runtime_error("Disable failure"); }
    void OnDestroy() override { ++destroyed; }
    void OnPoolAcquire() override { ++acquired;transient=0;if(failAcquire)throw std::runtime_error("Acquire failure");if(acquireAction)acquireAction(*this); }
    void OnPoolRelease() override { ++released; }
    void OnTriggerEnter(EntityID) override { ++enters; }
};
struct RuntimeOnly:Component {};
struct DestroyAccess:Component {
    inline static bool releaseRejected=false,spawnRejected=false,createRejected=false,sceneEmpty=false;
    void OnDestroy() override {
        auto& scene=GetGameObject().GetScene();
        releaseRejected=!scene.ReleasePrefab(GetGameObject().GetID());
        sceneEmpty=scene.GetObjects().empty();
        try{scene.InstantiatePrefab("Unavailable");}catch(const std::logic_error&){spawnRejected=true;}
        try{scene.CreateGameObject("Too late");}catch(const std::logic_error&){createRejected=true;}
    }
};

void HierarchyAndReuse(const fs::path& directory) {
    ComponentRegistry registry;
    registry.Register<Probe>("Probe",[](Probe& probe,const PropertyVisitor& fields){fields("Value",&probe.value);});
    PrefabLibrary library(registry);library.SetDirectory(directory);
    PrefabPool pool(registry);
    Scene source;
    auto& outside=source.CreateGameObject("outside");outside.transform.position={10,2,-3};outside.transform.rotation={0,20,0};
    auto& root=source.CreateGameObject("root");root.isGroup=true;root.SetParent(&outside);root.transform.position={1,2,3};
    root.transform.rotation={15,25,35};root.transform.localScale={2,1,3};
    root.AddComponent<Probe>().value=42;
    root.AddComponent<Material>().albedoAsset="preserved-grass-asset";
    auto& child=source.CreateGameObject("child");child.SetParent(&outside);child.transform.position={-2,1,4};child.SetGroup(&root);
    child.AddComponent<Light>().enabled=false;
    auto& polygon=child.AddComponent<PolygonCollider>();polygon.center={1,2,-3};polygon.isTrigger=true;
    child.AddComponent<MeshRenderer>().shape=int(BasicShape::Cone);
    auto& leaf=source.CreateGameObject("leaf");leaf.SetParent(&child);leaf.transform.position={1,0,0};leaf.AddComponent<Rigidbody>().mass=9;
    auto& sphere=leaf.AddComponent<SphereCollider>();sphere.center={0,-.25f,1};sphere.radius=1.75f;sphere.enabled=false;
    SceneCodec codec(registry);auto savedScene=codec.Encode(source);
    Check(savedScene.find("Component \"Polygon Collider\"")!=std::string::npos&&savedScene.find("Component \"Sphere Collider\"")!=std::string::npos,
          "Both collider names are written to the scene");
    Check(codec.Encode(*codec.Decode(savedScene))==savedScene,"Scene round-trip retains both collider shapes and settings");
    auto rootWorld=root.GetWorldMatrix(),childWorld=child.GetWorldMatrix(),leafWorld=leaf.GetWorldMatrix();
    const auto originalAwake=Probe::awakened;
    auto path=library.Save(source,root.GetID(),"Hierarchy");
    Check(fs::is_regular_file(path)&&library.Names()==std::vector<std::string>{"Hierarchy"},"Prefab written and discovered");
    Check(Probe::awakened==originalAwake,"Saving captures values without constructing script instances");
    library.Clear();auto asset=library.Load("Hierarchy");
    Check(asset->ObjectCount()==3,"Only selected root and descendants captured");
    auto oldPrefab=asset->Encode();
    auto typeAt=oldPrefab.find("\"Physics\"");
    Check(typeAt!=std::string::npos,"Prefab writes the Physics component name");
    oldPrefab.replace(typeAt,std::string("\"Physics\"").size(),"\"Rigidbody\"");
    Check(PrefabAsset::Decode(oldPrefab,registry)->Encode()==asset->Encode(),"Legacy prefab upgrades its Physics name and preserves every field");
    Reject([&]{library.Save(source,root.GetID(),"../escape");},"Prefab path traversal rejected");
    Reject([&]{PrefabAsset::Decode(ReadFile(path)+"garbage",registry);},"Trailing prefab data rejected");

    Scene runtime;
    pool.Prewarm(runtime,asset,2);
    Check(runtime.GetObjectCount()==6&&pool.Available(runtime,asset)==2,"Pool prewarms complete hierarchies");
    for(auto& object:runtime.GetObjects())Check(object->IsPoolAvailable()&&!object->IsActive(),"Prewarmed nodes remain inactive");
    Check(SceneCodec(registry).Decode(SceneCodec(registry).Encode(runtime))->GetObjectCount()==0,"Dormant pool entries excluded from scene saves");
    const auto warmAwake=Probe::awakened;
    auto& instance=pool.Instantiate(runtime,asset,{20,30,40});auto instanceID=instance.GetID();auto* component=instance.GetComponent<Probe>();
    auto delta=Vector3(20,30,40)-Vector3(rootWorld[3]);
    Check(instance.IsPooled()&&!instance.IsPoolAvailable()&&!instance.GetParent(),"Acquired root is detached and active");
    Check(instanceID!=root.GetID()&&component->value==42&&Probe::awakened==warmAwake,"Fresh identity with prewarmed components");
    Check(instance.GetComponent<Material>()->albedoAsset=="preserved-grass-asset","Asset reference preserved");
    GameObject* copiedChild=nullptr;GameObject* copiedLeaf=nullptr;
    for(auto& object:runtime.GetObjects())if(object->GetGroup()==&instance)copiedChild=object.get();
    Check(copiedChild&&copiedChild->GetID()!=child.GetID()&&!copiedChild->GetParent(),"Internal group remapped and external parent detached");
    Check(pool.Root(runtime,copiedChild->GetID())==instanceID,"A prefab child resolves its owning root");
    for(auto& object:runtime.GetObjects())if(object->GetParent()==copiedChild)copiedLeaf=object.get();
    auto expectedChild=childWorld;expectedChild[3]+=Vector4(delta,0);
    auto expectedLeaf=leafWorld;expectedLeaf[3]+=Vector4(delta,0);
    Check(Near(copiedChild->GetWorldMatrix(),expectedChild)&&copiedLeaf&&Near(copiedLeaf->GetWorldMatrix(),expectedLeaf),"Subtree world transforms preserved");
    Check(!copiedChild->GetComponent<Light>()->enabled&&copiedLeaf->GetComponent<Rigidbody>()->mass==9,"Child component properties preserved");
    auto* copiedPolygon=copiedChild->GetComponent<PolygonCollider>();
    auto* copiedSphere=copiedLeaf->GetComponent<SphereCollider>();
    Check(copiedPolygon&&copiedPolygon->isTrigger&&copiedPolygon->center==polygon.center&&copiedChild->GetComponent<MeshRenderer>()->shape==int(BasicShape::Cone),
          "PolygonCollider and its source mesh survive saving and instantiating a prefab");
    Check(copiedSphere&&!copiedSphere->enabled&&copiedSphere->center==sphere.center&&copiedSphere->radius==sphere.radius,
          "SphereCollider offset, radius and enabled state survive prefab persistence");
    runtime.Update(0);const auto startCount=Probe::started;const auto disableCount=Probe::disabledCalls;
    component->value=-1;component->transient=19;instance.name="edited";instance.transform.position={99,99,99};
    instance.AddComponent<RuntimeOnly>();
    auto addition=runtime.CreateGameObject("temporary child").GetID();runtime.Find(addition)->SetParent(&instance);
    Check(pool.Root(runtime,addition)==instanceID,"A new runtime child resolves its owning root");
    Check(pool.Release(runtime,instanceID)&&!pool.Release(runtime,instanceID),"Release succeeds once");
    Check(Probe::disabledCalls==disableCount+1&&!runtime.Find(addition)->IsActive(),"Release immediately disables descendants and callbacks");
    auto& reused=pool.Instantiate(runtime,asset,{4,5,6});
    Check(reused.GetID()==instanceID&&reused.GetComponent<Probe>()==component,"Pool reuses object and component identities");
    Check(component->value==42&&component->transient==0&&reused.name=="root","Serialized and custom transient state restored");
    Check(!reused.GetComponent<RuntimeOnly>()&&!runtime.Find(addition),"Runtime component and hierarchy additions removed");
    Check(glm::length(Vector3(reused.GetWorldMatrix()[3])-Vector3(4,5,6))<.001f,"Reuse moves the root to requested position");
    runtime.Update(0);Check(Probe::started==startCount,"Start remains once per component lifetime");

    Probe::failDisable=true;
    Reject([&]{pool.Release(runtime,instanceID);},"Release propagates callback failures");
    Probe::failDisable=false;
    Check(!runtime.Find(instanceID)->IsActive()&&pool.Available(runtime,asset)==2,"Failed release leaves a consistent inactive instance");
    Probe::failAcquire=true;
    Reject([&]{pool.Instantiate(runtime,asset);},"Acquire propagates callback failures");
    Probe::failAcquire=false;
    Check(runtime.GetObjectCount()==3&&pool.Available(runtime,asset)==1,"Failed acquire cleans the complete instance");

    auto& damaged=pool.Instantiate(runtime,asset);auto damagedID=damaged.GetID();
    EntityID descendant=0;
    for(auto& object:runtime.GetObjects())if(object.get()!=&damaged&&object->DependsOn(&damaged)){descendant=object->GetID();break;}
    runtime.Destroy(descendant);runtime.Flush();
    Check(!pool.Release(runtime,damagedID)&&runtime.GetObjectCount()==0,"Destroyed descendant invalidates and cleans its pool instance");
    auto newID=pool.Instantiate(runtime,asset).GetID();
    Check(newID!=damagedID,"Destroyed pooled object is replaced with fresh IDs");
    Probe::failDisable=true;pool.Clear(&runtime);Probe::failDisable=false;
    Check(runtime.GetObjectCount()==0,"Clear destroys all instances even when disable callbacks throw");

    Scene edit;
    auto placedID=library.Instantiate(edit,"Hierarchy",{1,2,3}).GetID();
    pool.Prewarm(edit,asset,1);pool.Clear(&edit);
    Check(edit.GetObjectCount()==3&&edit.Find(placedID)&&!edit.Find(placedID)->IsPooled(),"Edit placement survives pool clear");
    EntityID stale=0;
    {Scene discarded;stale=pool.Instantiate(discarded,asset).GetID();}
    Scene replacement;Check(!pool.Release(replacement,stale),"Stale scene handles are discarded without dereferencing the old scene");
    pool.Prewarm(replacement,asset,1);Check(pool.Available(replacement,asset)==1,"Pool can bind a replacement scene");pool.Clear(&replacement);
}

void CollisionReuse() {
    ComponentRegistry registry;
    registry.Register<Probe>("Probe",[](Probe& probe,const PropertyVisitor& fields){fields("Value",&probe.value);});
    Scene source;auto& root=source.CreateGameObject("trigger");root.AddComponent<Probe>();root.AddComponent<BoxCollider>().isTrigger=true;
    auto asset=PrefabAsset::Capture(source,root.GetID(),registry);
    Scene scene;scene.CreateGameObject("obstacle").AddComponent<BoxCollider>();PrefabPool pool(registry);PhysicsSystem physics;
    auto id=pool.Instantiate(scene,asset).GetID();auto enters=Probe::enters;
    physics.Update(scene,1.f/60);Check(Probe::enters==enters+1,"Acquired trigger participates in physics");
    pool.Release(scene,id);Check(pool.Instantiate(scene,asset).GetID()==id,"Collision test reuses same ID");
    physics.Update(scene,1.f/60);Check(Probe::enters==enters+2,"Reused trigger begins a fresh collision lifecycle");
    pool.Release(scene,id);physics.Update(scene,1.f/60);
    Check(CollisionSystem::Update(scene)==0,"Released colliders do not participate in physics");pool.Clear(&scene);
}

void ReentrantCallbacks() {
    ComponentRegistry registry;
    registry.Register<Probe>("Probe",[](Probe& probe,const PropertyVisitor& fields){fields("Value",&probe.value);});
    Scene source;source.CreateGameObject("recursive spawn").AddComponent<Probe>();
    auto asset=PrefabAsset::Capture(source,source.GetObjects().front()->GetID(),registry);
    Scene scene;PrefabPool pool(registry);bool nested=false;EntityID nestedID=0;
    Probe::acquireAction=[&](Probe& probe){
        Check(!pool.Release(scene,probe.GetGameObject().GetID()),"A partially activated instance cannot release itself");
        if(!nested){nested=true;nestedID=pool.Instantiate(scene,asset).GetID();}
    };
    auto id=pool.Instantiate(scene,asset).GetID();Probe::acquireAction={};
    Check(id!=nestedID&&scene.GetObjectCount()==2,"Nested acquire gets a distinct instance");pool.Clear(&scene);
    Probe::acquireAction=[&](Probe&){pool.Clear(&scene);};
    Reject([&]{pool.Instantiate(scene,asset);},"Clearing a pool inside its lifecycle callback is rejected safely");
    Probe::acquireAction={};Check(scene.GetObjectCount()==0,"Rejected reentrant clear cleans the interrupted acquisition");
}
void SceneDestruction() {
    bool calledOldBinding=false;
    {
        Scene scene;
        scene.SetPrefabCallbacks([&](const std::string&,Vector3)->GameObject&{calledOldBinding=true;return scene.CreateGameObject();},
                                 [&](EntityID){calledOldBinding=true;return true;});
        scene.CreateGameObject("destruction callback").AddComponent<DestroyAccess>();
    }
    Check(!calledOldBinding&&DestroyAccess::releaseRejected&&DestroyAccess::spawnRejected&&DestroyAccess::createRejected&&DestroyAccess::sceneEmpty,
          "Scene teardown keeps callbacks alive but disconnected and prevents creation from OnDestroy");
}
}

int main(int argc,char** argv) {
    try{
        Check(argc==2,"Expected test output directory");
        HierarchyAndReuse(fs::path(argv[1])/NewAssetID());CollisionReuse();ReentrantCallbacks();SceneDestruction();
        std::cout<<"Prefab persistence, hierarchy, pooling, lifecycle and stale-handle tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
