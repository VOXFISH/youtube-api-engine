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
int main(){try{
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
