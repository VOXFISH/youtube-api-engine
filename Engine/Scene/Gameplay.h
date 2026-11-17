#pragma once
#include "Rice/Benchmark.h"
#include "Components.h"
#include "SpatialBVH.h"
#include "Audio/Audio.h"
#include "Input/Input.h"
#include <map>
#include <set>
#include <functional>
namespace Rice {
struct Rigidbody:Component {Vector3 velocity{0};float mass=1,restitution=0;bool useGravity=true,kinematic=false;};
struct SpriteAnimator:Component {int columns=1,rows=1,frames=1;float fps=8;bool loop=true,playing=true;float clock=0;int frame=0;
    void OnPoolAcquire() override{ RICE_BENCHMARK_FUNCTION(); clock=0;frame=0;}
    void Update(float dt) override{ RICE_BENCHMARK_FUNCTION(); if(!playing)return;clock+=dt;int total=std::clamp(frames,1,std::clamp(columns,1,1024)*std::clamp(rows,1,1024));clock=std::fmod(clock,100000.f);int next=int(clock*glm::clamp(fps,0.f,120.f));frame=loop?next%total:std::min(next,total-1);}
};
struct UIText:Component {std::string text="Text";Vector2 position{20,20};Color color{1};float size=20;};
struct UIButton:Component {std::string text="Button";Vector2 position{20,60},size{150,40};std::string action="Increment";bool clicked=false;std::function<void()> onClick;};
struct PlayerController:Component {float speed=3;};
struct InputActions {
    std::map<std::string,bool> current,previous;
    void Bind(const std::string& action,int key){ RICE_BENCHMARK_FUNCTION(); if(action.empty()||key<32||key>348)throw std::invalid_argument("Invalid action binding");keys[action]=key;}
    void BeginFrame(bool captured=false){ RICE_BENCHMARK_FUNCTION(); previous=current;for(auto& [name,key]:keys)current[name]=!captured&&Input::IsKeyPressed(static_cast<KeyCode>(key));}
    bool Pressed(const std::string& name)const { RICE_BENCHMARK_FUNCTION(); return State(current,name)&&!State(previous,name);}
    bool Released(const std::string& name)const { RICE_BENCHMARK_FUNCTION(); return !State(current,name)&&State(previous,name);}
    static bool State(const std::map<std::string,bool>& values,const std::string& name){ RICE_BENCHMARK_FUNCTION(); auto it=values.find(name);return it!=values.end()&&it->second;}
    std::map<std::string,int> keys{{"Left",65},{"Right",68},{"Forward",87},{"Backward",83},{"Jump",32}};
    bool Held(const std::string& action)const{ RICE_BENCHMARK_FUNCTION(); return State(current,action);}
    float Axis(const std::string& negative,const std::string& positive)const{ RICE_BENCHMARK_FUNCTION(); return float(Held(positive))-float(Held(negative));}
};
class PhysicsSystem {
public:
    struct BroadphaseStats { uint64_t possiblePairs=0,candidatePairs=0,boundsRefreshes=0,treeBuilds=0,treeRefits=0,fallbackSteps=0; };
    void SetBroadphaseEnabled(bool enabled){broadphaseEnabled=enabled;}
    const BroadphaseStats& GetBroadphaseStats()const{return broadphaseStats;}
private:
    using Pair=std::pair<EntityID,EntityID>;
    using Generations=std::pair<uint64_t,uint64_t>;
    std::map<Pair,bool> previous;
    std::map<Pair,Generations> previousGenerations;
    float accumulator=0;
    bool broadphaseEnabled=true;
    BroadphaseStats broadphaseStats;
    SpatialBVH broadphase;
    std::vector<SpatialBounds> bodyBounds;
    std::vector<EntityID> bodyIDs;
    void SyncBroadphase(const std::vector<GameObject*>& bodies){
        bool membershipChanged=bodyIDs.size()!=bodies.size(),boundsChanged=false;
        if(!membershipChanged)for(size_t i=0;i<bodies.size();++i)
            if(bodyIDs[i]!=bodies[i]->GetID()){membershipChanged=true;break;}
        bodyBounds.resize(bodies.size());bodyIDs.resize(bodies.size());
        for(size_t i=0;i<bodies.size();++i){
            auto [low,high]=bodies[i]->GetComponent<BoxCollider>()->GetBounds();
            boundsChanged|=bodyBounds[i].low!=low||bodyBounds[i].high!=high;
            bodyBounds[i]={low,high};bodyIDs[i]=bodies[i]->GetID();
        }
        ++broadphaseStats.boundsRefreshes;
        if(membershipChanged){broadphase.Build(bodyBounds);++broadphaseStats.treeBuilds;}
        else if(boundsChanged){broadphase.Refit(bodyBounds);++broadphaseStats.treeRefits;}
    }
    static void Notify(Scene& scene,Pair pair,bool trigger,int phase,const Generations* expected=nullptr){
        RICE_BENCHMARK_FUNCTION();
        for(int side=0;side<2;++side)if(auto* obj=scene.Find(side?pair.second:pair.first))obj->EachComponent([&](Component& c){ RICE_BENCHMARK_FUNCTION(); if(!c.enabled||obj->IsPoolAvailable()||(expected&&obj->GetPoolGeneration()!=(side?expected->second:expected->first)))return;auto other=side?pair.first:pair.second;
            if(trigger){if(phase==0)c.OnTriggerEnter(other);else if(phase==1)c.OnTriggerStay(other);else c.OnTriggerExit(other);}
            else{if(phase==0)c.OnCollisionEnter(other);else if(phase==1)c.OnCollisionStay(other);else c.OnCollisionExit(other);}});
    }
    static float InverseMass(GameObject& obj){ RICE_BENCHMARK_FUNCTION(); auto* r=obj.GetComponent<Rigidbody>();return r&&r->enabled&&!r->kinematic?1.f/std::max(.001f,r->mass):0.f;}
    void Step(Scene& scene,float dt){
        RICE_BENCHMARK_FUNCTION();
        for(auto& obj:scene.GetObjects())if(obj->IsActive())if(auto* r=obj->GetComponent<Rigidbody>();r&&r->enabled){if(!r->kinematic&&r->useGravity)r->velocity.y-=9.81f*dt;obj->TranslateWorld(r->velocity*dt);}
        std::vector<GameObject*> bodies;for(auto& obj:scene.GetObjects())if(auto* c=obj->GetComponent<BoxCollider>()){c->overlapping=false;if(obj->IsActive()&&c->enabled)bodies.push_back(obj.get());}
        std::map<Pair,bool> contacts;
        std::map<Pair,Generations> generations;
        broadphaseStats.possiblePairs+=bodies.size()*(bodies.size()-(bodies.empty()?0:1))/2;
        auto resolve=[&](size_t i,size_t j,const SpatialBounds& ab,const SpatialBounds& bb){
            ++broadphaseStats.candidatePairs;
            auto& a=*bodies[i];auto& b=*bodies[j];auto* ca=a.GetComponent<BoxCollider>();auto* cb=b.GetComponent<BoxCollider>();
            auto amin=ab.low,amax=ab.high,bmin=bb.low,bmax=bb.high;Vector3 depth=glm::min(amax,bmax)-glm::max(amin,bmin);
            if(glm::any(glm::lessThan(depth,Vector3(0))))return false;
            ca->overlapping=cb->overlapping=true;Pair pair=std::minmax(a.GetID(),b.GetID());bool trigger=ca->isTrigger||cb->isTrigger;contacts[pair]=trigger;
            generations[pair]=a.GetID()==pair.first?Generations{a.GetPoolGeneration(),b.GetPoolGeneration()}:Generations{b.GetPoolGeneration(),a.GetPoolGeneration()};
            if(trigger)return false;
            float ia=InverseMass(a),ib=InverseMass(b);if(ia+ib==0)return false;
            int axis=depth.x<depth.y?0:1;if(depth.z<depth[axis])axis=2;
            Vector3 normal(0);normal[axis]=(amin[axis]+amax[axis]<bmin[axis]+bmax[axis])?-1.f:1.f;
            a.TranslateWorld(normal*depth[axis]*ia/(ia+ib));b.TranslateWorld(-normal*depth[axis]*ib/(ia+ib));
            auto* ra=a.GetComponent<Rigidbody>();auto* rb=b.GetComponent<Rigidbody>();
            Vector3 va=ra?ra->velocity:Vector3(0),vb=rb?rb->velocity:Vector3(0);float relative=glm::dot(va-vb,normal);
            if(relative<0){float bounce=std::max(ra?ra->restitution:0.f,rb?rb->restitution:0.f);float impulse=-(1+glm::clamp(bounce,0.f,1.f))*relative/(ia+ib);if(ia)ra->velocity+=normal*impulse*ia;if(ib)rb->velocity-=normal*impulse*ib;}
            return depth[axis]>0.f;
        };
        auto resolveRemaining=[&](size_t firstI,size_t firstJ){
            for(size_t i=firstI;i<bodies.size();++i)
                for(size_t j=i==firstI?firstJ:i+1;j<bodies.size();++j){
                    auto [amin,amax]=bodies[i]->GetComponent<BoxCollider>()->GetBounds();
                    auto [bmin,bmax]=bodies[j]->GetComponent<BoxCollider>()->GetBounds();
                    resolve(i,j,{amin,amax},{bmin,bmax});
                }
        };
        if(broadphaseEnabled){
            SyncBroadphase(bodies);
            std::vector<size_t> candidates;
            // A full refresh touches N bounds and the tree. Keep that work below
            // a conservative fraction of exhaustive pair work. Nonoverlapping
            // and trigger-only scenes never
            // spend this budget because they do not require position corrections.
            const size_t refreshBudget=std::max(size_t(1),bodies.empty()?size_t(0):(bodies.size()-1)/16);
            size_t corrections=0;bool fallback=false;
            for(size_t i=0;i<bodies.size()&&!fallback;++i){
                size_t after=i;
                auto query=[&]{
                    candidates.clear();
                    broadphase.QueryOverlaps(bodyBounds[i],[&](size_t j){if(j>after)candidates.push_back(j);});
                    std::sort(candidates.begin(),candidates.end());
                };
                query();
                for(size_t next=0;next<candidates.size();){
                    const size_t j=candidates[next++];after=j;
                    if(resolve(i,j,bodyBounds[i],bodyBounds[j])){
                        if(++corrections>=refreshBudget){
                            // Continue exactly where the legacy i,j loop would
                            // continue; previously skipped pairs must not return.
                            ++broadphaseStats.fallbackSteps;
                            resolveRemaining(i,j+1);fallback=true;break;
                        }
                        // Sequential resolution can create a later contact, including through
                        // parent/group movement. Refresh all bounds, then resume the original
                        // i,j ordering strictly after the pair that was just processed.
                        SyncBroadphase(bodies);query();next=0;
                    }
                }
            }
        }else resolveRemaining(0,1);
        if(scene.settings.ground&&scene.settings.groundCollision){
            for(auto* object:bodies){
                auto* collider=object->GetComponent<BoxCollider>();auto* body=object->GetComponent<Rigidbody>();
                if(collider->isTrigger||!body||!body->enabled||body->kinematic)continue;
                auto [low,high]=collider->GetBounds();
                if(low.y<0){object->TranslateWorld({0,-low.y,0});collider->overlapping=true;
                    if(body->velocity.y<0)body->velocity.y=-body->velocity.y*glm::clamp(body->restitution,0.f,1.f);
                }
            }
        }
        for(auto& [pair,trigger]:contacts){
            auto old=previous.find(pair);
            bool same=old!=previous.end()&&previousGenerations.at(pair)==generations.at(pair);
            if(same&&old->second!=trigger)Notify(scene,pair,old->second,2);
            Notify(scene,pair,trigger,same&&old->second==trigger?1:0);
        }
        for(auto& [pair,trigger]:previous)if(!contacts.contains(pair))Notify(scene,pair,trigger,2,&previousGenerations.at(pair));
        previous=std::move(contacts);
        previousGenerations=std::move(generations);
    }
public:
    void Reset(){ RICE_BENCHMARK_FUNCTION(); previous.clear();previousGenerations.clear();accumulator=0;bodyIDs.clear();bodyBounds.clear();broadphase=SpatialBVH{};broadphaseStats={};}
    void Update(Scene& scene,float dt){ RICE_BENCHMARK_FUNCTION(); if(!std::isfinite(dt)||dt<0)throw std::invalid_argument("Invalid physics dt");broadphaseStats={};accumulator+=std::min(dt,.1f);while(accumulator>=1.f/60){Step(scene,1.f/60);accumulator-=1.f/60;}}
};
}
