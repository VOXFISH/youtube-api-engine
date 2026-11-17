#pragma once
#include "Project/SceneCodec.h"
#include <cmath>
#include <array>
#include <map>
#include <iomanip>

// Presentation-only motion. Each sphere travels a fixed path from the light
// and loops; it is a visible ray marker, not a simulated or emitting light.
struct LightRayParticle : Rice::MonoBehaviour {
    Rice::Vector3 origin{0},direction{1,0,0};
    float travelDistance=5,periodSeconds=3,phase=0,diameter=.12f;
private:
    double elapsed=0;
    void ApplyPose(){
        const double period=std::isfinite(periodSeconds)?std::max(double(periodSeconds),.05):3.;
        const double offset=std::isfinite(phase)?double(phase):0.;
        double progress=std::fmod(elapsed/period+offset,1.);
        if(progress<0)progress+=1.;
        const float length=glm::length(direction);
        const auto axis=std::isfinite(length)&&length>1e-6f?direction/length:Rice::Vector3(1,0,0);
        const float distance=std::isfinite(travelDistance)?std::max(travelDistance,0.f):0.f;
        auto& transform=GetGameObject().transform;
        transform.position=origin+axis*(distance*float(progress));
        transform.localScale=Rice::Vector3(std::isfinite(diameter)?std::max(diameter,.001f):.12f);
    }
public:
    void Start() override {elapsed=0;ApplyPose();}
    void Update(float deltaTime) override {
        if(std::isfinite(deltaTime)&&deltaTime>0)elapsed+=deltaTime;
        ApplyPose();
    }
};

// One reusable visual sample on an emission path. The controller updates all
// samples after cube motion; these components do not simulate light transport.
struct PresentationBall : Rice::Component {
    Rice::Vector3 direction{1,0,0};
    float maxDistance=8,delaySeconds=.1f,speed=2.7f,diameter=.1f,repeatSeconds=3.4f;
};

struct ShadowPresentation : Rice::MonoBehaviour {
    Rice::EntityID source=0,steve=0,cube=0,status=0,oldBoundsGroup=0,currentBoundsGroup=0;
    bool autoMotion=true;
    float warmupSeconds=3,moveSeconds=2,holdSeconds=3;
    Rice::Vector3 offset{.9f,.25f,-.2f};
    struct Stats {
        size_t pathRebuilds=0,rayQueries=0,meshBuilds=0,boundsRefits=0,visibleBalls=0;
        size_t shadowFaces=0,shadowUpdates=0,changedCasters=0,affectedLights=0;
        size_t lastShadowFaces=0;
        double lastShadowRebuild=-1;
    };
private:
    struct Target {
        Rice::EntityID id=0;bool valid=false,built=false;
        int shape=-1;uint64_t revision=0;const Rice::Vector3* positions=nullptr;size_t positionCount=0;
        Rice::Matrix4 world{1},inverse{1};Rice::SpatialBounds local,bounds;
        Rice::MeshIntersection triangles;
    };
    struct BallState {Rice::Vector3 direction{0};float maximum=0,distance=0;};
    std::array<Target,2> targets;
    std::vector<size_t> targetOrder;
    Rice::SpatialBVH targetTree;
    std::map<Rice::EntityID,BallState> ballStates;
    Rice::Vector3 basePosition{0},baseRotation{0},lastSource{0};
    Rice::SpatialBounds previousCubeBounds;
    bool initialized=false,sourceKnown=false,lastGround=false,cubeBoundsKnown=false;
    double elapsed=0,lastCubeChange=-10;
    Stats stats;
    static bool Finite(Rice::Vector3 value){return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);}
    static float Positive(float value,float fallback,float minimum=.001f){return std::isfinite(value)?std::max(value,minimum):fallback;}
    static bool SameBounds(const Rice::SpatialBounds& a,const Rice::SpatialBounds& b){return a.low==b.low&&a.high==b.high;}
    void DrawBounds(Rice::EntityID groupID,const Rice::SpatialBounds& bounds,bool visible){
        auto& scene=GetGameObject().GetScene();auto* group=scene.Find(groupID);if(!group)return;
        group->SetActive(visible);if(!visible)return;
        size_t edge=0;
        for(auto& object:scene.GetObjects())if(object->GetParent()==group){
            if(edge>=12)break;
            int axis=int(edge/4),a=int(edge/2)%2,b=int(edge)%2;++edge;
            int u=(axis+1)%3,v=(axis+2)%3;
            auto center=(bounds.low+bounds.high)*.5f;Rice::Vector3 scale(.018f);
            scale[axis]=bounds.high[axis]-bounds.low[axis]+.018f;
            center[u]=a?bounds.high[u]:bounds.low[u];center[v]=b?bounds.high[v]:bounds.low[v];
            object->SetWorldMatrix(glm::translate(Rice::Matrix4(1),center)*glm::scale(Rice::Matrix4(1),scale));
            if(auto* mesh=object->GetComponent<Rice::MeshRenderer>())mesh->castsShadow=false;
        }
    }
    bool RefreshTargets(){
        auto& scene=GetGameObject().GetScene();const std::array<Rice::EntityID,2> ids{steve,cube};
        std::vector<size_t> order;std::vector<Rice::SpatialBounds> bounds;
        bool changed=false,boundsChanged=false;
        for(size_t i=0;i<targets.size();++i){
            auto& target=targets[i];auto* object=scene.Find(ids[i]);
            auto* mesh=object?object->GetComponent<Rice::MeshRenderer>():nullptr;
            bool valid=object&&object->IsActive()&&mesh&&mesh->enabled&&mesh->castsShadow&&mesh->color.a>=.1f&&
                glm::all(glm::greaterThan(glm::abs(object->transform.localScale),Rice::Vector3(.00001f)));
            Rice::Matrix4 world=valid?object->GetWorldMatrix():Rice::Matrix4(1);
            if(valid&&(!std::isfinite(glm::determinant(world))||std::abs(glm::determinant(world))<1e-8f))valid=false;
            if(target.id!=ids[i]){target.built=false;changed=true;}
            if(target.valid!=valid)changed=true;
            target.id=ids[i];target.valid=valid;if(!valid)continue;
            const bool custom=mesh->shape==int(Rice::BasicShape::Custom);
            uint64_t revision=custom?mesh->custom.revision:0;
            auto* positions=custom?mesh->custom.positions.data():nullptr;
            size_t positionCount=custom?mesh->custom.positions.size():0;
            const bool rebuild=!target.built||target.shape!=mesh->shape||target.revision!=revision||
                target.positions!=positions||target.positionCount!=positionCount;
            auto oldBounds=target.bounds;
            bool moved=!target.built||target.world!=world;
            if(rebuild){
                auto geometry=custom&&!mesh->custom.Empty()?mesh->custom:Rice::ShapeMesh::FromPrimitive(custom?Rice::BasicShape::Cube:static_cast<Rice::BasicShape>(std::clamp(mesh->shape,0,5)));
                target.triangles.Rebuild(geometry);auto [low,high]=geometry.Bounds();target.local={low,high};
                target.shape=mesh->shape;target.revision=revision;target.positions=positions;target.positionCount=positionCount;
                target.built=true;++stats.meshBuilds;changed=true;
            }
            if(moved||rebuild){
                target.world=world;target.inverse=glm::inverse(world);target.bounds=Rice::SpatialBounds::Transform(world,target.local);
                boundsChanged=boundsChanged||!SameBounds(oldBounds,target.bounds);changed=true;
                if(i==1){
                    // Keep the last settled pose during an entire movement,
                    // instead of drawing two nearly coincident frame bounds.
                    if(!cubeBoundsKnown||elapsed-lastCubeChange>.15)previousCubeBounds=cubeBoundsKnown?oldBounds:target.bounds;
                    cubeBoundsKnown=true;lastCubeChange=elapsed;
                }
            }
            order.push_back(i);bounds.push_back(target.bounds);
        }
        if(order!=targetOrder){targetTree.Build(bounds);targetOrder=std::move(order);changed=true;}
        else if(boundsChanged){targetTree.Refit(bounds);++stats.boundsRefits;}
        if(targets[1].valid){
            DrawBounds(currentBoundsGroup,targets[1].bounds,true);
            DrawBounds(oldBoundsGroup,previousCubeBounds,cubeBoundsKnown&&!SameBounds(previousCubeBounds,targets[1].bounds));
        }else{DrawBounds(currentBoundsGroup,{},false);DrawBounds(oldBoundsGroup,{},false);}
        return changed;
    }
    float Trace(Rice::Vector3 origin,Rice::Vector3 direction,float maximum){
        float nearest=maximum;
        targetTree.TraceRay(origin,direction,nearest,[&](size_t index,float& limit){
            const auto& target=targets[targetOrder[index]];
            auto localOrigin=Rice::Vector3(target.inverse*Rice::Vector4(origin,1));
            auto localDirection=Rice::Vector3(target.inverse*Rice::Vector4(direction,0));
            if(auto hit=target.triangles.Hit(localOrigin,localDirection,limit))limit=*hit;
        });
        if(lastGround&&direction.y<-.000001f){float hit=-origin.y/direction.y;if(hit>0)nearest=std::min(nearest,hit);}
        ++stats.rayQueries;return std::max(0.f,nearest);
    }
public:
    const Stats& GetStats()const{return stats;}
    double Elapsed()const{return elapsed;}
    std::optional<float> BallDistance(Rice::EntityID id)const{
        auto found=ballStates.find(id);return found==ballStates.end()?std::nullopt:std::optional<float>(found->second.distance);
    }
    void Start()override{
        elapsed=0;stats={};initialized=true;sourceKnown=false;cubeBoundsKnown=false;lastCubeChange=-10;
        targets={};targetOrder.clear();targetTree.Build({});ballStates.clear();
        if(auto* object=GetGameObject().GetScene().Find(cube)){basePosition=object->transform.position;baseRotation=object->transform.rotation;}
        LateUpdate(0);
    }
    void Update(float dt)override{
        if(!initialized)Start();if(std::isfinite(dt)&&dt>0)elapsed+=dt;
        if(!autoMotion)return;
        auto* object=GetGameObject().GetScene().Find(cube);if(!object)return;
        const double warmup=std::isfinite(warmupSeconds)?std::max(warmupSeconds,0.f):3;
        const double move=Positive(moveSeconds,2,.1f),hold=Positive(holdSeconds,3,0.f);
        float factor=0;
        if(elapsed>warmup){
            double t=std::fmod(elapsed-warmup,2*move+2*hold);
            if(t<move)factor=float(t/move);
            else if(t<move+hold)factor=1;
            else if(t<2*move+hold)factor=1-float((t-move-hold)/move);
            factor=factor*factor*(3-2*factor);
        }
        object->transform.position=basePosition+(Finite(offset)?offset:Rice::Vector3(0))*factor;
        object->transform.rotation=baseRotation+Rice::Vector3(12,40,16)*factor;
    }
    void LateUpdate(float)override{
        if(!initialized)return;
        stats.rayQueries=stats.meshBuilds=stats.boundsRefits=stats.visibleBalls=0;
        auto& scene=GetGameObject().GetScene();auto* emitter=scene.Find(source);
        Rice::Vector3 origin=emitter?emitter->GetWorldPivot():Rice::Vector3(0);
        if(emitter)if(auto* light=emitter->GetComponent<Rice::Light>())origin=light->WorldPosition();
        if(!Finite(origin))origin=Rice::Vector3(0);
        bool changed=RefreshTargets()||!sourceKnown||origin!=lastSource||lastGround!=scene.settings.ground;
        lastSource=origin;sourceKnown=true;lastGround=scene.settings.ground;
        struct Ball {Rice::GameObject* object;PresentationBall* settings;};std::vector<Ball> balls;
        for(auto& object:scene.GetObjects())if(object->IsActive())if(auto* ball=object->GetComponent<PresentationBall>();ball&&ball->enabled){
            balls.push_back({object.get(),ball});
            float length=glm::length(ball->direction);auto direction=std::isfinite(length)&&length>1e-6f?ball->direction/length:Rice::Vector3(1,0,0);
            float maximum=Positive(ball->maxDistance,8,0);
            auto found=ballStates.find(object->GetID());
            if(found==ballStates.end()||found->second.direction!=direction||found->second.maximum!=maximum){
                ballStates[object->GetID()]={direction,maximum,maximum};changed=true;
            }
        }
        std::erase_if(ballStates,[&](const auto& entry){return std::none_of(balls.begin(),balls.end(),[&](const Ball& value){return value.object->GetID()==entry.first;});});
        if(changed){
            struct Query {Rice::Vector3 direction;float maximum,distance;};std::vector<Query> queries;
            for(auto& [id,path]:ballStates){
                (void)id;auto query=std::find_if(queries.begin(),queries.end(),[&](const Query& q){return q.direction==path.direction&&q.maximum==path.maximum;});
                if(query!=queries.end())path.distance=query->distance;
                else{path.distance=Trace(origin,path.direction,path.maximum);queries.push_back({path.direction,path.maximum,path.distance});}
            }
            ++stats.pathRebuilds;
        }
        for(auto [object,ball]:balls){
            const auto& path=ballStates.at(object->GetID());
            double delay=std::isfinite(ball->delaySeconds)?std::max(ball->delaySeconds,0.f):0;
            double age=elapsed-delay;float distance=age>0?float(std::fmod(age,Positive(ball->repeatSeconds,3.4f,.1f)))*Positive(ball->speed,2.7f):0;
            float diameter=Positive(ball->diameter,.1f);
            bool visible=emitter&&emitter->IsActive()&&age>0&&distance>diameter*.5f&&distance<path.distance-diameter*.5f;
            if(auto* mesh=object->GetComponent<Rice::MeshRenderer>()){mesh->enabled=visible;mesh->castsShadow=false;}
            object->SetWorldMatrix(glm::translate(Rice::Matrix4(1),origin+path.direction*distance)*glm::scale(Rice::Matrix4(1),Rice::Vector3(diameter)));
            if(visible)++stats.visibleBalls;
        }
    }
    void ObserveRenderer(const Rice::SceneRenderer& renderer){
        if(!initialized)return;
        auto shadow=renderer.GetShadowStats();auto changes=renderer.GetShadowInvalidationStats();
        stats.shadowFaces=shadow.faces;stats.shadowUpdates=shadow.updates;
        stats.changedCasters=changes.changedCasters;stats.affectedLights=changes.affectedLights;
        if(shadow.faces){stats.lastShadowFaces=shadow.faces;stats.lastShadowRebuild=elapsed;}
        auto* object=GetGameObject().GetScene().Find(status);auto* text=object?object->GetComponent<Rice::UIText>():nullptr;if(!text)return;
        std::ostringstream out;out<<"POINT LIGHT / LIVE SHADOW CACHE\n";
        const bool recentlyUpdated=stats.lastShadowRebuild>=0&&elapsed-stats.lastShadowRebuild<.2;
        out<<(recentlyUpdated?"DEPTH UPDATED":"DEPTH CACHE REUSED")<<"   |   NOW: "<<stats.shadowFaces<<" faces\n";
        if(stats.lastShadowRebuild>=0)out<<"LAST REBUILD: "<<stats.lastShadowFaces<<" faces / "<<std::fixed<<std::setprecision(1)<<(elapsed-stats.lastShadowRebuild)<<" s ago\n";
        out<<"RAY PATH UPDATES: "<<stats.pathRebuilds<<"   |   BVH RAYS NOW: "<<stats.rayQueries<<"\n";
        out<<"Visible balls: "<<stats.visibleBalls<<"   |   "<<(autoMotion?"AUTO MOVE / HOLD":"MANUAL CUBE")<<"\n";
        out<<"Green = current cube AABB   |   Amber = previous settled pose";
        text->text=out.str();text->color=recentlyUpdated?Rice::Color(1,.83f,.3f,1):Rice::Color(.45f,1,.83f,1);
    }
};

inline void ObservePresentation(Rice::Scene& scene,const Rice::SceneRenderer& renderer){
    for(auto& object:scene.GetObjects())if(object->IsActive())if(auto* demo=object->GetComponent<ShadowPresentation>();demo&&demo->enabled)demo->ObserveRenderer(renderer);
}

inline void RegisterPresentationComponents(Rice::ComponentRegistry& registry){
    registry.Register<LightRayParticle>("Light Ray Particle",[](auto& component,const auto& fields){
        fields("Origin",&component.origin);
        fields("Direction",&component.direction);
        fields("Travel distance",&component.travelDistance);
        fields("Period seconds",&component.periodSeconds);
        fields("Phase",&component.phase);
        fields("Diameter",&component.diameter);
    });
    registry.Register<PresentationBall>("Presentation Ball",[](auto& component,const auto& fields){
        fields("Direction",&component.direction);fields("Max distance",&component.maxDistance);
        fields("Delay seconds",&component.delaySeconds);fields("Speed",&component.speed);
        fields("Diameter",&component.diameter);fields("Repeat seconds",&component.repeatSeconds);
    });
    registry.Register<ShadowPresentation>("Shadow Presentation",[](auto& component,const auto& fields){
        fields("Source",&component.source);fields("Steve",&component.steve);fields("Cube",&component.cube);fields("Status",&component.status);
        fields("Old bounds group",&component.oldBoundsGroup);fields("Current bounds group",&component.currentBoundsGroup);
        fields("Auto motion",&component.autoMotion);fields("Warmup seconds",&component.warmupSeconds);
        fields("Move seconds",&component.moveSeconds);fields("Hold seconds",&component.holdSeconds);fields("Offset",&component.offset);
    });
}
