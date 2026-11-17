#pragma once
#include "Rice/Benchmark.h"
#include "Rice/Core.h"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <optional>
namespace Rice {
class Texture2D;
using EntityID = uint64_t;
struct Transform {
    Vector3 position{0}, rotation{0}, localScale{1};
    Vector3 pivot{.5f}; // Unclamped fractions of the object's local geometry bounds.
    Matrix4 affine{1}; // Retains shear when changing transform spaces.
    Matrix4 GetMatrix() const { RICE_BENCHMARK_FUNCTION(); return glm::translate(Matrix4(1),position)*glm::mat4_cast(Quaternion(glm::radians(rotation)))*glm::scale(Matrix4(1),localScale)*affine; }
    void SetMatrix(const Matrix4& matrix){
        RICE_BENCHMARK_FUNCTION();
        if(std::abs(glm::determinant(matrix))<1e-8f)throw std::runtime_error("Cannot decompose singular transform");
        position=Vector3(matrix[3]);
        Vector3 x=glm::normalize(Vector3(matrix[0]));
        Vector3 y=glm::normalize(Vector3(matrix[1])-x*glm::dot(x,Vector3(matrix[1])));
        Vector3 z=glm::cross(x,y);
        localScale={glm::dot(x,Vector3(matrix[0])),glm::dot(y,Vector3(matrix[1])),glm::dot(z,Vector3(matrix[2]))};
        rotation=glm::degrees(glm::eulerAngles(glm::quat_cast(glm::mat3(x,y,z))));affine=Matrix4(1);
        auto trs=GetMatrix();if(std::abs(glm::determinant(trs))<1e-8f)throw std::runtime_error("Singular transform");
        affine=glm::inverse(trs)*matrix;
    }
    void Translate(const Vector3& offset) { RICE_BENCHMARK_FUNCTION(); position+=offset; }
};
class Scene;
class GameObject;
class Component {
    friend class GameObject;
    GameObject* owner=nullptr;
    bool started=false, wasEnabled=false, removed=false;
public:
    bool enabled=true;
    virtual ~Component()=default;
    GameObject& GetGameObject() const { RICE_BENCHMARK_FUNCTION(); if(!owner)throw std::logic_error("Unattached component"); return *owner; }
    virtual void Awake() { RICE_BENCHMARK_FUNCTION(); } virtual void Start() { RICE_BENCHMARK_FUNCTION(); } virtual void OnEnable() { RICE_BENCHMARK_FUNCTION(); } virtual void OnDisable() { RICE_BENCHMARK_FUNCTION(); } virtual void OnDestroy() { RICE_BENCHMARK_FUNCTION(); }
    virtual void LateUpdate(float) {}
    virtual void Update(float) { RICE_BENCHMARK_FUNCTION(); } virtual void Render() { RICE_BENCHMARK_FUNCTION(); }
    virtual void OnCollisionEnter(EntityID) { RICE_BENCHMARK_FUNCTION(); } virtual void OnCollisionStay(EntityID) { RICE_BENCHMARK_FUNCTION(); } virtual void OnCollisionExit(EntityID) { RICE_BENCHMARK_FUNCTION(); }
    virtual void OnTriggerEnter(EntityID) { RICE_BENCHMARK_FUNCTION(); } virtual void OnTriggerStay(EntityID) { RICE_BENCHMARK_FUNCTION(); } virtual void OnTriggerExit(EntityID) { RICE_BENCHMARK_FUNCTION(); }
    // Reset transient state here when a pooled instance starts a new use.
    virtual void OnPoolAcquire() { RICE_BENCHMARK_FUNCTION(); }
    virtual void OnPoolRelease() { RICE_BENCHMARK_FUNCTION(); }
    virtual std::optional<std::pair<Vector3,Vector3>> GeometryBounds()const{return std::nullopt;}
};
struct Anchor : Component {
    EntityID child=0;
    Transform transform;
    bool showInEditor=true;
};
using MonoBehaviour=Component;
class Scene;
class GameObject {
    friend class Scene;
    inline static std::atomic<EntityID> nextID{1};
    EntityID id=nextID++;
    Scene* owningScene=nullptr;
    uint64_t* hierarchyRevision=nullptr;
    std::vector<Scope<Component>> components;
    GameObject* parent=nullptr;
    GameObject* group=nullptr;
    bool pendingDestroy=false;
    bool pooled=false, poolAvailable=false;
    uint64_t poolGeneration=0;
    inline static bool worldCacheEnabled=true;
    mutable bool worldCached=false;
    mutable Transform cachedTransform;
    mutable Vector3 cachedPivot{0};
    mutable Matrix4 cachedParent{1},cachedWorld{1};
    void HierarchyChanged(){if(hierarchyRevision)++*hierarchyRevision;}

    template<class F>void NotifyComponents(F fn) {
        RICE_BENCHMARK_FUNCTION();
        std::exception_ptr failure;
        EachComponent([&](Component& component) {
            try { fn(component); }
            catch(...) { if(!failure)failure=std::current_exception(); }
        });
        if(failure)std::rethrow_exception(failure);
    }
public:
    explicit GameObject(std::string value):name(std::move(value)) { RICE_BENCHMARK_FUNCTION(); }
    GameObject(const GameObject&)=delete; GameObject& operator=(const GameObject&)=delete;
    ~GameObject() {
        RICE_BENCHMARK_FUNCTION();
        for(auto& component:components){
            try{if(component->wasEnabled)component->OnDisable();}catch(...){}
            try{component->OnDestroy();}catch(...){}
        }
    }
    std::string name; Transform transform; bool activeSelf=true;
    bool isGroup=false;
    EntityID previousGroup=0;
    Matrix4 groupOffset{1},groupParentBind{1},groupCommonBind{1};
    EntityID GetID() const { RICE_BENCHMARK_FUNCTION(); return id;}
    Scene& GetScene() const { RICE_BENCHMARK_FUNCTION(); if(!owningScene)throw std::logic_error("Object has no scene");return *owningScene;}
    bool IsPendingDestroy() const { RICE_BENCHMARK_FUNCTION(); return pendingDestroy;}
    bool IsActive() const { RICE_BENCHMARK_FUNCTION(); return activeSelf && !pendingDestroy && !poolAvailable && (!parent || parent->IsActive()) && (!group || group->IsActive());}
    bool IsPooled() const { RICE_BENCHMARK_FUNCTION(); return pooled; }
    bool IsPoolAvailable() const {
        RICE_BENCHMARK_FUNCTION();
        return poolAvailable || (parent && parent->IsPoolAvailable()) || (group && group->IsPoolAvailable());
    }
    uint64_t GetPoolGeneration() const { RICE_BENCHMARK_FUNCTION(); return poolGeneration; }
    void SetPoolState(bool owned,bool available) {
        RICE_BENCHMARK_FUNCTION();
        if(owned && !available && poolAvailable)++poolGeneration;
        if(poolAvailable!=(owned&&available))HierarchyChanged();
        pooled=owned;poolAvailable=owned&&available;
    }
    void SynchronizeActivation() {
        RICE_BENCHMARK_FUNCTION();
        NotifyComponents([&](Component& component) {
            bool active=IsActive()&&component.enabled;
            if(active==component.wasEnabled)return;
            component.wasEnabled=active;
            if(active)component.OnEnable();else component.OnDisable();
        });
    }
    void NotifyPoolAcquire() {
        RICE_BENCHMARK_FUNCTION();
        NotifyComponents([](Component& component){component.OnPoolAcquire();});
    }
    void NotifyPoolRelease() {
        RICE_BENCHMARK_FUNCTION();
        NotifyComponents([](Component& component){component.OnPoolRelease();});
    }
    GameObject* GetParent() const { RICE_BENCHMARK_FUNCTION(); return parent;}
    GameObject* GetGroup()const{ RICE_BENCHMARK_FUNCTION(); return group;}
    GameObject* GetHierarchyParent()const{ RICE_BENCHMARK_FUNCTION(); return group?group:parent;}
    bool DependsOn(const GameObject* value)const{ RICE_BENCHMARK_FUNCTION(); return this==value||(parent&&parent->DependsOn(value))||(group&&group->DependsOn(value));}
    Vector3 GetLocalPivot()const{
        RICE_BENCHMARK_FUNCTION();
        for(const auto& c:components)if(!c->removed)if(auto bounds=c->GeometryBounds())return bounds->first+(bounds->second-bounds->first)*transform.pivot;
        return transform.pivot-Vector3(.5f);
    }
    const Anchor* ParentAnchor()const{
        if(parent)if(auto* anchor=parent->GetComponent<Anchor>();anchor&&anchor->enabled&&anchor->child==id)return anchor;
        return nullptr;
    }
    Matrix4 GetPivotMatrix()const{return GetWorldMatrix()*glm::translate(Matrix4(1),GetLocalPivot());}
    Vector3 GetWorldPivot()const{return Vector3(GetPivotMatrix()[3]);}
    Matrix4 GetParentMatrix()const{
        RICE_BENCHMARK_FUNCTION();
        // A bound child's pivot is constrained to the anchor, even when its
        // stored local translation or an editor group changes.
        if(auto* anchor=ParentAnchor())return parent->GetPivotMatrix()*anchor->transform.GetMatrix()*glm::translate(Matrix4(1),-GetLocalPivot()-transform.position);
        auto base=parent?parent->GetWorldMatrix():Matrix4(1);
        if(!group)return base;
        // A parent already inherited by the group must not be applied twice.
        GameObject* common=nullptr;
        for(auto* p=parent;p;p=p->GetHierarchyParent())if(group->DependsOn(p)){common=p;break;}
        auto relative=common?glm::inverse(common->GetWorldMatrix())*base:base;
        return group->GetWorldMatrix()*groupOffset*glm::inverse(groupParentBind)*groupCommonBind*relative;
    }
    Quaternion GetWorldRotation()const{
        RICE_BENCHMARK_FUNCTION();
        auto local=Quaternion(glm::radians(transform.rotation));
        if(auto* anchor=ParentAnchor())return glm::normalize(parent->GetWorldRotation()*Quaternion(glm::radians(anchor->transform.rotation))*local);
        auto base=parent?parent->GetWorldRotation():Quaternion(1,0,0,0);
        if(!group)return glm::normalize(base*local);
        // Bind matrices are fixed snapshots. Strip scale/shear from each once
        // conceptually, then compose only live rotation channels recursively.
        auto rotation=[](const Matrix4& matrix){ RICE_BENCHMARK_FUNCTION(); Transform decomposed;decomposed.SetMatrix(matrix);return Quaternion(glm::radians(decomposed.rotation));};
        GameObject* common=nullptr;
        for(auto* p=parent;p;p=p->GetHierarchyParent())if(group->DependsOn(p)){common=p;break;}
        auto relative=common?glm::inverse(common->GetWorldRotation())*base:base;
        return glm::normalize(group->GetWorldRotation()*rotation(groupOffset)*glm::inverse(rotation(groupParentBind))*rotation(groupCommonBind)*relative*local);
    }
    // Public transform fields remain writable. Compare actual dependencies rather
    // than relying on hierarchy revisions (which do not track those writes).
    static void SetWorldCacheEnabled(bool enabled){worldCacheEnabled=enabled;}
    static bool WorldCacheEnabled(){return worldCacheEnabled;}
    Matrix4 GetWorldMatrix() const {
        RICE_BENCHMARK_FUNCTION();
        auto p=GetLocalPivot();auto base=GetParentMatrix();
        if(worldCacheEnabled&&worldCached&&cachedParent==base&&cachedPivot==p&&
           cachedTransform.position==transform.position&&cachedTransform.rotation==transform.rotation&&
           cachedTransform.localScale==transform.localScale&&cachedTransform.affine==transform.affine)return cachedWorld;
        auto value=base*glm::translate(Matrix4(1),p)*transform.GetMatrix()*glm::translate(Matrix4(1),-p);
        if(worldCacheEnabled){cachedParent=base;cachedPivot=p;cachedTransform=transform;cachedWorld=value;worldCached=true;}
        return value;
    }
    void SetWorldMatrix(const Matrix4& value){ RICE_BENCHMARK_FUNCTION(); auto base=GetParentMatrix();if(std::abs(glm::determinant(base))<1e-8f)throw std::runtime_error("Singular parent transform");auto p=GetLocalPivot();transform.SetMatrix(glm::translate(Matrix4(1),-p)*glm::inverse(base)*value*glm::translate(Matrix4(1),p));}
    void SetParent(GameObject* value) { RICE_BENCHMARK_FUNCTION(); if(value&&value->owningScene!=owningScene)throw std::invalid_argument("Parent must belong to same scene");if(value&&value->DependsOn(this))throw std::invalid_argument("Hierarchy cycle");if(parent==value)return;if(parent)if(auto* a=parent->GetComponent<Anchor>();a&&a->child==id)a->child=0;parent=value;HierarchyChanged();}
    void SetGroup(GameObject* value,bool preserveWorld=true){
        RICE_BENCHMARK_FUNCTION();
        if(value&&(!value->isGroup||value->owningScene!=owningScene||value->DependsOn(this)))throw std::invalid_argument("Invalid group or group cycle");
        auto world=GetWorldMatrix();auto base=parent?parent->GetWorldMatrix():Matrix4(1);
        if(value&&(std::abs(glm::determinant(value->GetWorldMatrix()))<1e-8f||std::abs(glm::determinant(base))<1e-8f))throw std::runtime_error("Singular group transform");
        if(group!=value)HierarchyChanged();group=value;groupParentBind=base;groupCommonBind=Matrix4(1);
        if(group)for(auto* p=parent;p;p=p->GetHierarchyParent())if(group->DependsOn(p)){groupCommonBind=p->GetWorldMatrix();break;}
        groupOffset=value?glm::inverse(value->GetWorldMatrix())*base:Matrix4(1);
        if(preserveWorld)SetWorldMatrix(world);
    }
    void TranslateWorld(Vector3 delta) { RICE_BENCHMARK_FUNCTION(); auto m=GetParentMatrix();if(std::abs(glm::determinant(m))<1e-8f)throw std::runtime_error("Singular parent transform");transform.Translate(Vector3(glm::inverse(m)*Vector4(delta,0)));}
    template<class T,class... Args>T& AddComponent(Args&&... args) {
        RICE_BENCHMARK_FUNCTION();
        if constexpr(std::is_same_v<T,Transform>)return transform;
        else {static_assert(std::is_base_of_v<Component,T>);if(auto* old=GetComponent<T>())return *old;
            auto value=CreateScope<T>(std::forward<Args>(args)...);auto* result=value.get();result->owner=this;components.push_back(std::move(value));result->Awake();return *result;}
    }
    template<class T>T* GetComponent() const { RICE_BENCHMARK_FUNCTION(); if constexpr(std::is_same_v<T,Transform>)return const_cast<Transform*>(&transform);else {for(auto& c:components)if(!c->removed)if(auto* result=dynamic_cast<T*>(c.get()))return result;return nullptr;}}
    template<class T>void RemoveComponent(){ RICE_BENCHMARK_FUNCTION(); static_assert(!std::is_same_v<T,Transform>);if(auto* c=GetComponent<T>())c->removed=true;}
    void RemoveComponent(Component& component) {
        RICE_BENCHMARK_FUNCTION();
        if(component.owner!=this)throw std::invalid_argument("Component belongs to another object");
        component.removed=true;
    }
    void FlushComponents(){
        RICE_BENCHMARK_FUNCTION();
        std::exception_ptr failure;
        auto invoke=[&](auto callback){try{callback();}catch(...){if(!failure)failure=std::current_exception();}};
        auto count=components.size();
        for(size_t i=0;i<count;++i)if(components[i]->removed){
            auto* component=components[i].get();
            bool wasEnabled=component->wasEnabled;component->wasEnabled=false;
            if(wasEnabled)invoke([&]{component->OnDisable();});
            invoke([&]{component->OnDestroy();});
        }
        std::erase_if(components,[](auto& c){return c->removed;});
        if(failure)std::rethrow_exception(failure);
    }
    void SetActive(bool value){ RICE_BENCHMARK_FUNCTION(); activeSelf=value;}
    template<class F>void EachComponent(F fn){ RICE_BENCHMARK_FUNCTION(); auto n=components.size();for(size_t i=0;i<n;++i)if(!components[i]->removed)fn(*components[i]);}
    void Update(float dt){ RICE_BENCHMARK_FUNCTION(); EachComponent([&](Component& c){ RICE_BENCHMARK_FUNCTION(); bool active=IsActive()&&c.enabled;if(active!=c.wasEnabled){c.wasEnabled=active;if(active)c.OnEnable();else c.OnDisable();}if(!active)return;if(!c.started){c.started=true;c.Start();}if(IsActive()&&c.enabled&&!c.removed)c.Update(dt);});}
    void Render(){ RICE_BENCHMARK_FUNCTION(); if(IsActive())EachComponent([](Component& c){ RICE_BENCHMARK_FUNCTION(); if(c.enabled)c.Render();});}
};
struct SceneSettings {
    bool skybox=false;
    std::string skyboxAsset;
    bool directionalLight=false;
    Color lightColor{1};float lightIntensity=1;Vector3 lightDirection{-1,-1,-1};
    bool ground=false,groundCollision=true;
    Vector3 axes{1},grids{1}; // XY, YZ, ZX; editor-only visibility.
};
class Scene {
    inline static std::atomic<uint64_t> nextInstance{1};
    uint64_t instance=nextInstance++;
    uint64_t hierarchyRevision=0;
    std::vector<Scope<GameObject>> objects;
    std::function<GameObject&(const std::string&,Vector3)> instantiatePrefab;
    std::function<bool(EntityID)> releasePrefab;
    std::function<void(const std::string&,size_t)> prewarmPrefab;
    bool tearingDown=false;
public:
    ~Scene() {
        RICE_BENCHMARK_FUNCTION();
        tearingDown=true;
        instantiatePrefab={};releasePrefab={};prewarmPrefab={};
        // Script destruction sees an empty scene while its callback objects are alive.
        auto discarded=std::move(objects);
        discarded.clear();
    }
    SceneSettings settings;
    Ref<Texture2D> skyboxTexture;
    std::string loadedSkyboxAsset;
    void SetPrefabCallbacks(std::function<GameObject&(const std::string&,Vector3)> instantiate,
                            std::function<bool(EntityID)> release,
                            std::function<void(const std::string&,size_t)> prewarm={}) {
        RICE_BENCHMARK_FUNCTION();
        if(tearingDown)throw std::logic_error("Scene is being destroyed");
        instantiatePrefab=std::move(instantiate);releasePrefab=std::move(release);prewarmPrefab=std::move(prewarm);
    }
    GameObject& InstantiatePrefab(const std::string& name,Vector3 position=Vector3(0)) {
        RICE_BENCHMARK_FUNCTION();
        if(tearingDown||!instantiatePrefab)throw std::logic_error("Prefab spawning is not configured for this scene");
        return instantiatePrefab(name,position);
    }
    bool ReleasePrefab(EntityID root) {
        RICE_BENCHMARK_FUNCTION();
        return !tearingDown&&releasePrefab&&releasePrefab(root);
    }
    void PrewarmPrefab(const std::string& name,size_t count) {
        RICE_BENCHMARK_FUNCTION();
        if(tearingDown||!prewarmPrefab)throw std::logic_error("Prefab pooling is not configured for this scene");
        prewarmPrefab(name,count);
    }
    uint64_t GetInstanceID()const{ RICE_BENCHMARK_FUNCTION(); return instance;}
    uint64_t GetHierarchyRevision()const{return hierarchyRevision;}
    GameObject& CreateGameObject(std::string name="GameObject",EntityID id=0){ RICE_BENCHMARK_FUNCTION(); if(tearingDown)throw std::logic_error("Scene is being destroyed");if(id&&Find(id))throw std::invalid_argument("Duplicate entity ID");objects.push_back(CreateScope<GameObject>(std::move(name)));auto& o=*objects.back();o.owningScene=this;o.hierarchyRevision=&hierarchyRevision;++hierarchyRevision;if(id){o.id=id;GameObject::nextID.store(std::max(GameObject::nextID.load(),id+1));}return o;}
    GameObject* Find(EntityID id) const{ RICE_BENCHMARK_FUNCTION(); for(auto& o:objects)if(o->id==id&&!o->pendingDestroy)return o.get();return nullptr;}
    void Destroy(EntityID id){ RICE_BENCHMARK_FUNCTION(); if(auto* o=Find(id)){o->pendingDestroy=true;++hierarchyRevision;for(auto& c:objects)if(c->parent==o||c->group==o)Destroy(c->id);}}
    void FlushDestroyed(){ RICE_BENCHMARK_FUNCTION(); for(auto& o:objects)if(auto* a=o->GetComponent<Anchor>();a&&a->child&&!Find(a->child))a->child=0;std::erase_if(objects,[](auto& o){return o->pendingDestroy;}); }
    void Flush(){ RICE_BENCHMARK_FUNCTION(); for(auto& o:objects)o->FlushComponents();FlushDestroyed();}
    void PlaceSibling(EntityID source,EntityID reference,bool after){
        RICE_BENCHMARK_FUNCTION();
        if(source==reference)return;auto* a=Find(source);auto* b=Find(reference);
        if(!a||!b||a->GetHierarchyParent()!=b->GetHierarchyParent())throw std::invalid_argument("Sibling placement requires the same hierarchy parent");
        auto from=std::find_if(objects.begin(),objects.end(),[&](const auto& obj){ RICE_BENCHMARK_FUNCTION(); return obj->GetID()==source;});
        auto value=std::move(*from);objects.erase(from);
        auto destination=std::find_if(objects.begin(),objects.end(),[&](const auto& obj){ RICE_BENCHMARK_FUNCTION(); return obj->GetID()==reference;});
        if(after)++destination;objects.insert(destination,std::move(value));++hierarchyRevision;
    }
    // Undo restores the persisted order, including objects from different parents.
    // Objects outside the captured document (for example pooled reserves) retain
    // their relative order after the restored objects.
    void RestoreObjectOrder(const std::vector<EntityID>& order){
        RICE_BENCHMARK_FUNCTION();
        std::vector<Scope<GameObject>> reordered;reordered.reserve(objects.size());
        for(auto id:order){
            auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& value){return value&&value->id==id;});
            if(found!=objects.end())reordered.push_back(std::move(*found));
        }
        for(auto& object:objects)if(object)reordered.push_back(std::move(object));
        objects=std::move(reordered);++hierarchyRevision;
    }
    const auto& GetObjects() const{ RICE_BENCHMARK_FUNCTION(); return objects;}
    size_t GetObjectCount() const{ RICE_BENCHMARK_FUNCTION(); return objects.size();}
    void Update(float dt){ RICE_BENCHMARK_FUNCTION(); if(!std::isfinite(dt)||dt<0)throw std::invalid_argument("Invalid delta time");auto n=objects.size();for(size_t i=0;i<n;++i)objects[i]->Update(dt);}
    void LateUpdate(float dt){auto n=objects.size();for(size_t i=0;i<n;++i)if(objects[i]->IsActive())objects[i]->EachComponent([&](Component& c){if(c.enabled)c.LateUpdate(dt);});}
    void Render(){ RICE_BENCHMARK_FUNCTION(); auto n=objects.size();for(size_t i=0;i<n;++i)objects[i]->Render();}
};
}
