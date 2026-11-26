#include "Scene/Gameplay.h"
#include <iostream>
#include <tuple>
using namespace Rice;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct ContactRecorder:Component {
    std::vector<std::tuple<int,EntityID>> events;
    void OnCollisionEnter(EntityID id)override{events.emplace_back(0,id);}
    void OnCollisionStay(EntityID id)override{events.emplace_back(1,id);}
    void OnCollisionExit(EntityID id)override{events.emplace_back(2,id);}
    void OnTriggerEnter(EntityID id)override{events.emplace_back(3,id);}
    void OnTriggerStay(EntityID id)override{events.emplace_back(4,id);}
    void OnTriggerExit(EntityID id)override{events.emplace_back(5,id);}
};
static GameObject& Body(Scene& scene,EntityID id,Vector3 position,bool dynamic=false){
    auto& object=scene.CreateGameObject("Body",id);object.transform.position=position;
    object.AddComponent<BoxCollider>();object.AddComponent<ContactRecorder>();
    if(dynamic)object.AddComponent<Rigidbody>().useGravity=false;
    return object;
}
static void Compare(Scene& reference,Scene& optimized){
    Check(reference.GetObjectCount()==optimized.GetObjectCount(),"Scene membership differs");
    for(auto& object:reference.GetObjects()){
        auto* other=optimized.Find(object->GetID());Check(other!=nullptr,"Missing object");
        auto a=object->GetWorldMatrix(),b=other->GetWorldMatrix();
        for(int c=0;c<4;++c)Check(glm::all(glm::lessThanEqual(glm::abs(a[c]-b[c]),Vector4(1e-6f))),"Sequential response differs");
        if(auto* body=object->GetComponent<Rigidbody>())
            Check(glm::all(glm::lessThanEqual(glm::abs(body->velocity-other->GetComponent<Rigidbody>()->velocity),Vector3(1e-6f))),"Velocity differs");
        Check(object->GetComponent<BoxCollider>()->overlapping==other->GetComponent<BoxCollider>()->overlapping,"Overlap state differs");
        Check(object->GetComponent<ContactRecorder>()->events==other->GetComponent<ContactRecorder>()->events,"Collision/trigger events differ");
    }
}
template<class Populate,class Mutate>
static void CompareSteps(Populate populate,Mutate mutate,int steps){
    Scene reference,optimized;populate(reference);populate(optimized);
    PhysicsSystem legacy,accelerated;legacy.SetBroadphaseEnabled(false);
    for(int frame=0;frame<steps;++frame){
        mutate(reference,frame);mutate(optimized,frame);
        legacy.Update(reference,1.f/60);accelerated.Update(optimized,1.f/60);
        Compare(reference,optimized);
    }
}

// Exercise both the editor overlap query and the fixed-step event path.
static void CheckTriggerLifecycle(Scene& scene,GameObject& source,GameObject& probe,Vector3 contact,Vector3 gap){
    auto& recorder=probe.AddComponent<ContactRecorder>();
    PhysicsSystem system;
    const auto id=source.GetID();
    auto step=[&](Vector3 position,bool overlaps){
        probe.transform.position=position;
        for(bool bvh:{false,true})
            Check(CollisionSystem::Update(scene,bvh)==size_t(overlaps),"Overlap queries must use the collider geometry");
        system.Update(scene,1.f/60);
    };
    step(gap,false);
    Check(recorder.events.empty(),"Overlapping bounds alone do not fire a trigger");
    step(contact,true);
    step(contact,true);
    step(gap,false);
    Check(recorder.events==std::vector<std::tuple<int,EntityID>>{{3,id},{4,id},{5,id}},
          "Shape triggers deliver enter, stay, and exit for the same peer");
}

static void TestRoundContacts(){
    for(bool useBox:{false,true}){
        Scene scene;
        auto& source=scene.CreateGameObject("Trigger sphere");
        auto& sphere=source.AddComponent<SphereCollider>();sphere.radius=1;sphere.isTrigger=true;
        auto& probe=scene.CreateGameObject("Probe");
        if(useBox)probe.AddComponent<BoxCollider>().size=Vector3(.4f);
        else probe.AddComponent<SphereCollider>().radius=.2f;
        CheckTriggerLifecycle(scene,source,probe,{.9f,0,0},{.95f,.95f,.95f});
    }
}

static void TestPolygonContacts(){
    enum class ProbeShape { Sphere, Box, Polygon };
    for(auto probeShape:{ProbeShape::Sphere,ProbeShape::Box,ProbeShape::Polygon}){
        Scene scene;
        auto& source=scene.CreateGameObject("Two separate triangles");
        auto& polygon=source.AddComponent<PolygonCollider>();polygon.isTrigger=true;
        auto& probe=scene.CreateGameObject("Probe");
        if(probeShape==ProbeShape::Sphere)probe.AddComponent<SphereCollider>().radius=.2f;
        else if(probeShape==ProbeShape::Box)probe.AddComponent<BoxCollider>().size=Vector3(.4f);
        else {probe.AddComponent<PolygonCollider>();probe.AddComponent<MeshRenderer>();probe.transform.localScale=Vector3(.4f);}
        Check(CollisionSystem::Update(scene)==0,"PolygonCollider waits for a source mesh");
        auto& mesh=source.AddComponent<MeshRenderer>();mesh.shape=int(BasicShape::Custom);
        Check(!SceneRenderer::CustomArray(mesh)&&mesh.custom.Empty()&&CollisionSystem::Update(scene)==0,
              "Rendering an empty custom mesh leaves its polygon collider inactive");
        mesh.custom.positions={{-3,-1,0},{-1,-1,0},{-2,1,0},{1,-1,0},{3,-1,0},{2,1,0}};
        mesh.custom.indices={0,1,2,3,4,5};mesh.custom.uv.resize(6);mesh.custom.Touch();
        CheckTriggerLifecycle(scene,source,probe,{2,0,.05f},{0,0,.05f});

        // Move one triangle into the gap without changing vertex or index counts.
        for(int i=3;i<6;++i)mesh.custom.positions[i].x-=2;
        mesh.custom.Touch();
        Check(CollisionSystem::Update(scene)==1,"Editing mesh vertices updates polygon collision");
        source.transform.rotation.z=90;
        source.transform.position={4,3,0};
        Check(CollisionSystem::Update(scene)==0,"Moving the mesh moves its polygon collider");
        probe.transform.position=Vector3(source.GetWorldMatrix()*Vector4(0,0,.05f,1));
        Check(CollisionSystem::Update(scene)==1,"Transformed polygon remains collidable");

        // An object deep inside a closed shell has no surface intersection.
        source.transform.position=Vector3(0);source.transform.rotation=Vector3(0);probe.transform.position=Vector3(0);
        Check(CollisionSystem::Update(scene)==1,"The probe touches the edited triangle");
        ShapeMesh replacement=mesh.custom;
        for(auto& point:replacement.positions)point.x+=9;
        mesh.custom=std::move(replacement);
        Check(CollisionSystem::Update(scene)==0,"Replacing a mesh with the same revision and counts invalidates its collider");
        mesh.custom=ShapeMesh::FromPrimitive(BasicShape::Cube);mesh.custom.Touch();
        Check(CollisionSystem::Update(scene)==1,"A closed shell contains a smaller collider");
        mesh.custom.indices.resize(mesh.custom.indices.size()-6);
        mesh.custom.uv.resize(mesh.custom.indices.size());mesh.custom.Touch();
        Check(CollisionSystem::Update(scene)==0,"An open shell has no solid interior");
    }
}

static void TestDisconnectedShells(){
    Scene scene;
    auto& source=scene.CreateGameObject("Separated solid shells");source.AddComponent<PolygonCollider>();
    auto& mesh=source.AddComponent<MeshRenderer>();mesh.shape=int(BasicShape::Custom);
    auto cube=ShapeMesh::FromPrimitive(BasicShape::Cube);
    for(float offset:{-2.f,2.f}){
        uint32_t start=uint32_t(mesh.custom.positions.size());
        for(auto point:cube.positions)mesh.custom.positions.push_back(point+Vector3(offset,0,0));
        for(auto index:cube.indices)mesh.custom.indices.push_back(start+index);
    }
    mesh.custom.uv.resize(mesh.custom.indices.size());mesh.custom.Touch();
    auto& probe=scene.CreateGameObject("Inner sphere");probe.AddComponent<SphereCollider>().radius=.1f;
    Check(CollisionSystem::Update(scene)==0,"The gap between disconnected closed shells is empty");
    for(float x:{-2.f,2.f}){
        probe.transform.position.x=x;
        Check(CollisionSystem::Update(scene)==1,"Each disconnected closed shell has a solid interior");
    }
    probe.SetActive(false);scene.settings.ground=false;
    auto& moving=scene.CreateGameObject("Piece entering the gap-side face");
    moving.transform.position={1.4f,0,0};moving.transform.localScale=Vector3(.4f);
    moving.AddComponent<MeshRenderer>();moving.AddComponent<PolygonCollider>();
    moving.AddComponent<Rigidbody>().useGravity=false;
    PhysicsSystem physics;physics.Update(scene,1.f/60);
    Check(glm::length(moving.transform.position-Vector3(1.3f,0,0))<.001f,
          "Mesh contact pushes a piece back into the gap by its actual penetration depth");
}

static void TestShapeResponse(){
    Scene scene;
    auto& ramp=scene.CreateGameObject("Ramp");ramp.AddComponent<PolygonCollider>();
    auto& mesh=ramp.AddComponent<MeshRenderer>();mesh.shape=int(BasicShape::Custom);
    mesh.custom.positions={{2,0,0},{0,2,0},{0,0,2}};
    mesh.custom.indices={0,1,2};mesh.custom.uv.resize(3);mesh.custom.Touch();
    auto& ball=scene.CreateGameObject("Ball");ball.transform.position=Vector3(.8f);
    ball.AddComponent<SphereCollider>().radius=.3f;
    auto& body=ball.AddComponent<Rigidbody>();body.velocity=Vector3(-1);body.useGravity=false;body.restitution=1;
    PhysicsSystem system;system.Update(scene,1.f/60);
    Check(glm::length(body.velocity-Vector3(1))<.002f,"Sphere bounces along the triangle normal on a sloped mesh");
    Check(glm::all(glm::greaterThan(ball.transform.position,Vector3(.8f)))&&ramp.transform.position==Vector3(0),
          "Penetration correction moves the ball away from the fixed ramp");

    Scene floorScene;floorScene.settings.ground=true;
    auto& sphere=floorScene.CreateGameObject("Scaled sphere");
    sphere.transform.position={-2,.2f,0};sphere.transform.localScale={1,2,1};
    sphere.AddComponent<SphereCollider>().radius=.5f;sphere.AddComponent<Rigidbody>();
    auto& cube=floorScene.CreateGameObject("Polygon on ground");
    cube.transform.position={2,.25f,0};cube.transform.localScale={1,2,1};
    cube.AddComponent<PolygonCollider>();cube.AddComponent<MeshRenderer>();cube.AddComponent<Rigidbody>();
    PhysicsSystem groundSystem;
    for(int step=0;step<4;++step)groundSystem.Update(floorScene,1.f/60);
    Check(std::abs(sphere.transform.position.y-1.f)<.001f&&std::abs(cube.transform.position.y-1.f)<.001f,
          "Scaled spheres and mesh-derived polygons rest on the scene ground");
}

int main(){try{
    TestRoundContacts();TestPolygonContacts();TestDisconnectedShells();TestShapeResponse();
    {
        Scene scene;scene.settings.ground=true;
        auto& probe=Body(scene,1,{0,.25f,0},true);
        auto& trigger=Body(scene,2,{0,.5f,0});
        trigger.name="Portal";trigger.GetComponent<BoxCollider>()->isTrigger=true;
        PhysicsSystem system;system.TrackScriptContacts(probe.GetID());
        auto contacts=[&](Scripting::Phase phase){
            std::vector<std::string> result;
            Scripting::Contact contact;
            for(int i=0;system.ContactAt(probe.GetID(),int(phase),i,contact);++i){
                if(contact.isGround)Check(!contact.isTrigger&&contact.other==0,"Ground has a distinct contact context");
                else Check(contact.isTrigger&&contact.other==trigger.GetID(),"Trigger retains the other object identity");
                result.emplace_back(contact.name);
            }
            return result;
        };
        system.Update(scene,1.f/60);
        auto both=std::vector<std::string>{"Ground","Portal"};
        Check(contacts(Scripting::Phase::Collision)==both,"Ground and trigger record enter edges");
        Check(contacts(Scripting::Phase::Overlap)==both,"First contacts also overlap");
        Check(contacts(Scripting::Phase::Collision)==both,"Predicates are non-consuming");
        system.Update(scene,1.f/60);
        Check(contacts(Scripting::Phase::Collision).empty()&&contacts(Scripting::Phase::Overlap)==both,
              "Resting Ground and trigger contacts stay without repeating enter");
        probe.transform.position.y=4;
        system.Update(scene,1.f/60);
        Check(contacts(Scripting::Phase::OverlapEnd)==both&&contacts(Scripting::Phase::Overlap).empty(),
              "Separating from Ground and trigger emits both exits");
        system.Update(scene,0);
        Check(contacts(Scripting::Phase::OverlapEnd).empty(),"Edges expire even on frames without a physics step");
    }
    // Resolving pair (1,2) moves body 1 into body 3, which was outside the
    // original candidate set. A once-per-step pair list would miss this.
    CompareSteps([](Scene& scene){
        Body(scene,1,{0,0,0},true);Body(scene,2,{.8f,0,0});Body(scene,3,{-1.15f,0,0});
    },[](Scene&,int){},4);
    // A correction to a parent also changes a third body's bounds.
    CompareSteps([](Scene& scene){
        auto& parent=Body(scene,1,{0,0,0},true);Body(scene,2,{.8f,0,0});Body(scene,3,{-2.4f,0,0});
        auto& child=Body(scene,4,{-1.3f,0,0});child.SetParent(&parent);
    },[](Scene&,int){},4);
    CompareSteps([](Scene& scene){
        for(int i=0;i<64;++i){
            auto& object=Body(scene,EntityID(i+1),{float(i%8)*.9f,float((i/8)%4)*1.05f,float(i/32)*1.1f},i%3==0);
            object.transform.rotation={0,float(i%5)*9.f,float(i%4)*5.f};
            object.transform.localScale={i%7==0?-.8f:.8f,.7f,1.f};
            auto& collider=*object.GetComponent<BoxCollider>();collider.isTrigger=i%5==0;collider.center={.03f,0,0};
            if(auto* body=object.GetComponent<Rigidbody>()){body->velocity={.15f,-.1f,.07f};body->restitution=.3f;}
        }
    },[](Scene& scene,int frame){
        if(frame==2)scene.Find(2)->GetComponent<BoxCollider>()->enabled=false;
        if(frame==4)scene.Find(2)->GetComponent<BoxCollider>()->enabled=true;
        if(frame==5)scene.Find(3)->SetActive(false);
        if(frame==6)scene.Find(3)->SetActive(true);
        if(frame==7)scene.Find(4)->GetComponent<BoxCollider>()->size={2,1,1};
        if(frame==8)scene.Find(5)->transform.position={-20,0,0};
        if(frame==9)Body(scene,100,{0,0,0}).GetComponent<BoxCollider>()->isTrigger=true;
        if(frame==10)scene.Find(100)->SetPoolState(true,true);
        if(frame==11)scene.Find(100)->SetPoolState(true,false);
        if(frame==12)scene.Find(6)->GetComponent<BoxCollider>()->isTrigger=!scene.Find(6)->GetComponent<BoxCollider>()->isTrigger;
    },16);
    Scene sparse;for(int i=0;i<128;++i)Body(sparse,EntityID(i+1),{float(i)*3,0,0});
    PhysicsSystem system;system.Update(sparse,1.f/60);
    Check(system.GetBroadphaseStats().possiblePairs==8128,"Pair accounting");
    Check(system.GetBroadphaseStats().candidatePairs==0,"Sparse broadphase rejects all distant pairs");
    system.Update(sparse,1.f/60);
    Check(system.GetBroadphaseStats().treeBuilds==0&&system.GetBroadphaseStats().treeRefits==0,"Unchanged bounds reuse BVH");
    system.Reset();system.Update(sparse,1.f/60);Check(system.GetBroadphaseStats().treeBuilds==1,"Reset rebuilds broadphase");
    {
        Scene reference,dense;
        for(int i=0;i<64;++i){
            auto position=Vector3(float(i%4)*.15f,float((i/4)%4)*.15f,float(i/16)*.15f);
            Body(reference,EntityID(i+1),position,true);Body(dense,EntityID(i+1),position,true);
        }
        PhysicsSystem legacy,adaptive;legacy.SetBroadphaseEnabled(false);
        legacy.Update(reference,1.f/60);adaptive.Update(dense,1.f/60);Compare(reference,dense);
        Check(adaptive.GetBroadphaseStats().fallbackSteps==1,"Dense response switches to cached exhaustive remainder");
        Check(adaptive.GetBroadphaseStats().boundsRefreshes<=3,"Dense response bounds refresh budget");
        // The tree left stale by the fallback must be refreshed on the next step.
        legacy.Update(reference,1.f/60);adaptive.Update(dense,1.f/60);Compare(reference,dense);
    }
    std::cout<<"Spatial physics regression passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
