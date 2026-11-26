#pragma once

#include "SceneCodec.h"
#include "SceneLayers.h"
#include <memory>
#include <queue>
#include <unordered_map>

namespace Rice {

class PrefabAsset {
    struct Node {
        EntityID source=0,parent=0,group=0,previousGroup=0;
        std::string name;
        bool active=true,isGroup=false;
        Transform transform;
        Matrix4 offset{1},bind{1},common{1},world{1};
        std::vector<ComponentSnapshot> components;
    };
    std::vector<Node> nodes;
    std::vector<size_t> order;
    std::unordered_map<EntityID,size_t> indices;

    void Validate(ComponentRegistry& registry) {
        RICE_BENCHMARK_FUNCTION();
        if(nodes.empty()||nodes.size()>10000)throw std::runtime_error("Invalid prefab object count");
        indices.clear();order.clear();
        for(size_t i=0;i<nodes.size();++i){
            auto& node=nodes[i];
            if(!node.source||!indices.emplace(node.source,i).second)throw std::runtime_error("Duplicate prefab object ID");
            std::set<std::string> types;
            for(auto& component:node.components){
                auto* type=registry.Find(component.type);
                if(!type||!types.insert(type->name).second)
                    throw std::runtime_error("Unknown or duplicate prefab component: "+component.type);
                component.type=type->name;
            }
            std::erase_if(node.components,[&](const ComponentSnapshot& component){
                if(component.type!="Scene Layer")return false;
                SceneLayer layer;
                component.ReadInto(*registry.Find(component.type),layer);
                // Older prefabs could mark content groups as layers. Retain the
                // group and its children, and only instantiate explicit game-layer roots.
                return node.parent||node.group||!node.isGroup||layer.role!=SceneLayer::Game;
            });
        }
        if(nodes.front().parent||nodes.front().group)throw std::runtime_error("Prefab root must be detached");
        std::vector<size_t> waiting(nodes.size());
        std::vector<std::vector<size_t>> dependents(nodes.size());
        for(size_t i=0;i<nodes.size();++i){
            const auto& node=nodes[i];
            if(i&&!node.parent&&!node.group)throw std::runtime_error("Prefab contains a disconnected object");
            std::set<EntityID> dependencies{node.parent,node.group};dependencies.erase(0);
            for(auto dependency:dependencies){
                auto found=indices.find(dependency);
                if(found==indices.end())throw std::runtime_error("Prefab hierarchy reference is missing");
                if(dependency==node.group&&!nodes[found->second].isGroup)throw std::runtime_error("Prefab group is not a group object");
                dependents[found->second].push_back(i);++waiting[i];
            }
        }
        std::queue<size_t> ready;ready.push(0);
        while(!ready.empty()){
            auto index=ready.front();ready.pop();order.push_back(index);
            for(auto dependent:dependents[index])if(!--waiting[dependent])ready.push(dependent);
        }
        if(order.size()!=nodes.size())throw std::runtime_error("Prefab hierarchy contains a cycle");
    }

    static void SetWorld(GameObject& object,const Matrix4& world) {
        RICE_BENCHMARK_FUNCTION();
        // Preserve an exact existing local transform, including zero scale.
        auto current=object.GetWorldMatrix();
        bool same=true;
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(current[c][r]-world[c][r])>0.00001f)same=false;
        if(same)return;
        auto parent=object.GetParentMatrix();
        if(std::abs(glm::determinant(parent))<1e-8f)throw std::runtime_error("Cannot place prefab under a singular transform");
        auto pivot=object.GetLocalPivot();auto local=glm::translate(Matrix4(1),-pivot)*glm::inverse(parent)*world*glm::translate(Matrix4(1),pivot);
        if(std::abs(glm::determinant(local))>=1e-8f){object.transform.SetMatrix(local);return;}
        // Affine storage also represents a degenerate detached source transform.
        auto normalizedPivot=object.transform.pivot;object.transform=Transform{};object.transform.pivot=normalizedPivot;
        object.transform.position=Vector3(local[3]);
        local[3]=Vector4(0,0,0,1);
        object.transform.affine=local;
    }

public:
    size_t ObjectCount() const { RICE_BENCHMARK_FUNCTION(); return nodes.size(); }
    Vector3 OriginalPosition() const {
        RICE_BENCHMARK_FUNCTION();
        if(nodes.empty())throw std::logic_error("Prefab asset is empty");
        return Vector3(nodes.front().world[3]);
    }

    static std::shared_ptr<const PrefabAsset> Capture(Scene& scene,EntityID rootID,ComponentRegistry& registry) {
        RICE_BENCHMARK_FUNCTION();
        auto* root=scene.Find(rootID);
        if(!root||root->IsPoolAvailable())throw std::invalid_argument("Select an available scene object to save as a prefab");
        if(root->IsRequiredLayer())throw std::invalid_argument("Required layers cannot be saved as prefabs");
        auto asset=std::make_shared<PrefabAsset>();
        auto capture=[&](GameObject& object){
            RICE_BENCHMARK_FUNCTION();
            Node node;
            node.source=object.GetID();node.parent=object.GetParent()?object.GetParent()->GetID():0;
            node.group=object.GetGroup()?object.GetGroup()->GetID():0;node.previousGroup=object.previousGroup;
            node.name=object.name;node.active=object.activeSelf;node.isGroup=object.isGroup;node.transform=object.transform;
            node.offset=object.groupOffset;node.bind=object.groupParentBind;node.common=object.groupCommonBind;
            node.world=object.GetWorldMatrix();node.components=ComponentSnapshot::CaptureAll(object,registry);
            asset->nodes.push_back(std::move(node));
        };
        capture(*root);
        for(auto& object:scene.GetObjects())
            if(object.get()!=root&&!object->IsPendingDestroy()&&!object->IsPoolAvailable()&&object->DependsOn(root))capture(*object);
        std::set<EntityID> included;
        for(const auto& node:asset->nodes)included.insert(node.source);
        for(auto& node:asset->nodes){
            if(!included.contains(node.parent))node.parent=0;
            if(!included.contains(node.group))node.group=0;
            if(!included.contains(node.previousGroup))node.previousGroup=0;
        }
        asset->Validate(registry);
        return asset;
    }

    std::string Encode() const {
        RICE_BENCHMARK_FUNCTION();
        std::ostringstream output;output<<std::setprecision(9)<<"RICE_PREFAB 2\n"<<nodes.size()<<'\n';
        for(const auto& node:nodes){
            output<<node.source<<' '<<node.parent<<' '<<node.group<<' '<<node.previousGroup<<' '
                  <<node.active<<' '<<node.isGroup<<' '<<std::quoted(node.name)<<'\n';
            auto transform=node.transform;
            WriteProperty(output,&transform.position);WriteProperty(output,&transform.rotation);WriteProperty(output,&transform.localScale);WriteProperty(output,&transform.pivot);
            WriteMatrix(output,transform.affine);WriteMatrix(output,node.offset);WriteMatrix(output,node.bind);
            WriteMatrix(output,node.common);WriteMatrix(output,node.world);output<<'\n'<<node.components.size()<<'\n';
            for(const auto& component:node.components)output<<std::quoted(component.type)<<' '<<std::quoted(component.data)<<'\n';
        }
        return output.str();
    }

    static std::shared_ptr<const PrefabAsset> Decode(const std::string& text,ComponentRegistry& registry) {
        RICE_BENCHMARK_FUNCTION();
        if(text.size()>16*1024*1024)throw std::runtime_error("Prefab exceeds 16 MiB");
        std::istringstream input(text);std::string magic;int version=0;size_t count=0;
        input>>magic>>version>>count;
        if(!input||magic!="RICE_PREFAB"||(version!=1&&version!=2)||!count||count>10000)throw std::runtime_error("Invalid prefab header");
        auto asset=std::make_shared<PrefabAsset>();asset->nodes.resize(count);
        for(auto& node:asset->nodes){
            input>>node.source>>node.parent>>node.group>>node.previousGroup>>node.active>>node.isGroup>>std::quoted(node.name);
            ReadProperty(input,&node.transform.position);ReadProperty(input,&node.transform.rotation);ReadProperty(input,&node.transform.localScale);
            if(version>=2)ReadProperty(input,&node.transform.pivot);
            ReadMatrix(input,node.transform.affine);ReadMatrix(input,node.offset);ReadMatrix(input,node.bind);
            ReadMatrix(input,node.common);ReadMatrix(input,node.world);
            size_t components=0;input>>components;
            if(!input||components>registry.types.size())throw std::runtime_error("Invalid prefab component count");
            node.components.resize(components);
            for(auto& component:node.components)input>>std::quoted(component.type)>>std::quoted(component.data);
            if(!input)throw std::runtime_error("Truncated prefab");
        }
        input>>std::ws;
        if(!input.eof())throw std::runtime_error("Trailing prefab data");
        asset->Validate(registry);
        return asset;
    }

    static void Destroy(Scene& scene,const std::vector<EntityID>& objects) {
        RICE_BENCHMARK_FUNCTION();
        for(auto id:objects)scene.Destroy(id);
        scene.FlushDestroyed();
    }

    void Restore(Scene& scene,const std::vector<EntityID>& objects,ComponentRegistry& registry,Vector3 position,bool pooled) const {
        RICE_BENCHMARK_FUNCTION();
        if(objects.size()!=nodes.size()||!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))
            throw std::invalid_argument("Invalid prefab placement");
        std::vector<GameObject*> targets;targets.reserve(objects.size());
        for(auto id:objects){
            auto* object=scene.Find(id);
            if(!object)throw std::runtime_error("Prefab instance was destroyed");
            targets.push_back(object);object->SetPoolState(pooled,pooled);
        }
        // Detach before resetting groups, which may have been edited during use.
        for(auto* object:targets){object->SetParent(nullptr);object->SetGroup(nullptr,false);}
        for(size_t i=0;i<nodes.size();++i){
            const auto& node=nodes[i];auto& object=*targets[i];
            object.name=scene.UniqueObjectName(node.name,&object);object.transform=node.transform;object.activeSelf=node.active;object.isGroup=node.isGroup;
            object.previousGroup=node.previousGroup&&indices.contains(node.previousGroup)?objects[indices.at(node.previousGroup)]:0;
            object.groupOffset=node.offset;object.groupParentBind=node.bind;object.groupCommonBind=node.common;
            ComponentSnapshot::Restore(object,node.components,registry);
            RemapScriptBindings(object,[&](EntityID id){return indices.contains(id)?objects[indices.at(id)]:EntityID{};});
            if(auto* anchor=object.GetComponent<Anchor>())anchor->child=indices.contains(anchor->child)?objects[indices.at(anchor->child)]:0;
            if(auto* f=object.GetComponent<CameraFollower>())f->camera=indices.contains(f->camera)?objects[indices.at(f->camera)]:0;
        }
        auto offset=position-OriginalPosition();
        for(auto index:order){
            const auto& node=nodes[index];auto& object=*targets[index];
            if(node.parent)object.SetParent(targets[indices.at(node.parent)]);
            if(node.group){
                object.SetGroup(targets[indices.at(node.group)],false);
                object.groupOffset=node.offset;object.groupParentBind=node.bind;object.groupCommonBind=node.common;
            }
            auto world=node.world;world[3]+=Vector4(offset,0);SetWorld(object,world);
        }
    }

    std::vector<EntityID> Create(Scene& scene,ComponentRegistry& registry,Vector3 position,bool pooled) const {
        RICE_BENCHMARK_FUNCTION();
        if(nodes.empty())throw std::invalid_argument("Prefab asset is empty");
        std::vector<EntityID> created;created.reserve(nodes.size());
        try{
            for(const auto& node:nodes){auto& object=scene.CreateGameObject(node.name);created.push_back(object.GetID());object.SetPoolState(pooled,pooled);}
            Restore(scene,created,registry,position,pooled);
        }catch(...){Destroy(scene,created);throw;}
        return created;
    }
};

class PrefabLibrary {
    ComponentRegistry& registry;
    fs::path directory, legacyDirectory;
    std::map<std::string,std::shared_ptr<const PrefabAsset>> assets;

    fs::path Path(const std::string& name) const {
        RICE_BENCHMARK_FUNCTION();
        if(directory.empty())throw std::logic_error("Prefab directory is not configured");
        if(name.empty()||name=="."||name==".."||name.back()=='.'||name.back()==' '||name.find_first_of("/\\:*?\"<>|")!=name.npos||
           std::any_of(name.begin(),name.end(),[](unsigned char c){RICE_BENCHMARK_FUNCTION();return c<32;}))
            throw std::invalid_argument("Use a prefab name without path separators or reserved filename characters");
        std::u8string filename(name.begin(),name.end());filename+=u8".prefab";
        return directory/fs::path(filename);
    }
public:
    explicit PrefabLibrary(ComponentRegistry& registry):registry(registry){RICE_BENCHMARK_FUNCTION();}
    void SetDirectory(fs::path value,fs::path legacy={}) { RICE_BENCHMARK_FUNCTION(); directory=std::move(value);legacyDirectory=std::move(legacy);Clear(); }
    void Clear() { RICE_BENCHMARK_FUNCTION(); assets.clear(); }
    std::vector<std::string> Names() const {
        RICE_BENCHMARK_FUNCTION();
        std::vector<std::string> names;
        for(const auto& folder:{directory,legacyDirectory}){
            if(folder.empty()||!fs::is_directory(folder))continue;
            for(const auto& entry:fs::directory_iterator(folder))if(entry.is_regular_file()&&entry.path().extension()==".prefab"){
                auto name=entry.path().stem().u8string();names.emplace_back(name.begin(),name.end());
            }
        }
        std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());return names;
    }
    fs::path Save(Scene& scene,EntityID root,const std::string& name) {
        RICE_BENCHMARK_FUNCTION();
        auto path=Path(name);auto asset=PrefabAsset::Capture(scene,root,registry);
        auto text=asset->Encode();
        if(text.size()>16*1024*1024)throw std::runtime_error("Prefab exceeds 16 MiB");
        AtomicWrite(path,text);assets[name]=std::move(asset);return path;
    }
    std::shared_ptr<const PrefabAsset> Load(const std::string& name) {
        RICE_BENCHMARK_FUNCTION();
        auto path=Path(name);auto found=assets.find(name);
        if(found!=assets.end())return found->second;
        if(!fs::is_regular_file(path)&&!legacyDirectory.empty())path=legacyDirectory/path.filename();
        auto asset=PrefabAsset::Decode(ReadFile(path),registry);assets.emplace(name,asset);return asset;
    }
    GameObject& Instantiate(Scene& scene,const std::string& name,Vector3 position=Vector3(0)) {
        RICE_BENCHMARK_FUNCTION();
        auto objects=Load(name)->Create(scene,registry,position,false);
        return *scene.Find(objects.front());
    }
};

class PrefabPool {
    struct Instance {
        std::shared_ptr<const PrefabAsset> asset;
        std::vector<EntityID> objects;
        bool available=true;
        bool busy=false;
    };
    ComponentRegistry& registry;
    uint64_t sceneInstance=0;
    std::map<EntityID,Instance> instances;
    bool clearing=false;

    bool HasTransition() const {
        RICE_BENCHMARK_FUNCTION();
        return std::any_of(instances.begin(),instances.end(),[](const auto& pair){RICE_BENCHMARK_FUNCTION();return pair.second.busy;});
    }

    void Bind(Scene& scene) {
        RICE_BENCHMARK_FUNCTION();
        if(clearing)throw std::logic_error("Prefab pool is being cleared");
        if(sceneInstance==scene.GetInstanceID())return;
        if(HasTransition())throw std::logic_error("Cannot rebind a prefab pool from a lifecycle callback");
        instances.clear();sceneInstance=scene.GetInstanceID();
    }
    static bool Valid(Scene& scene,const Instance& instance) {
        RICE_BENCHMARK_FUNCTION();
        return std::all_of(instance.objects.begin(),instance.objects.end(),[&](EntityID id){RICE_BENCHMARK_FUNCTION();return scene.Find(id)!=nullptr;});
    }
    void Purge(Scene& scene) {
        RICE_BENCHMARK_FUNCTION();
        Bind(scene);
        for(auto it=instances.begin();it!=instances.end();){
            if(it->second.busy||Valid(scene,it->second)){++it;continue;}
            auto objects=std::move(it->second.objects);it=instances.erase(it);
            PrefabAsset::Destroy(scene,objects);
        }
    }
    static void Deactivate(Scene& scene,Instance& instance,bool notify) {
        RICE_BENCHMARK_FUNCTION();
        instance.available=true;instance.busy=true;
        for(auto id:instance.objects)if(auto* object=scene.Find(id))object->SetPoolState(true,true);
        std::exception_ptr failure;
        for(auto id:instance.objects)if(auto* object=scene.Find(id)){
            try{object->SynchronizeActivation();}catch(...){if(!failure)failure=std::current_exception();}
            if(notify)try{object->NotifyPoolRelease();}catch(...){if(!failure)failure=std::current_exception();}
        }
        instance.busy=false;
        if(failure)std::rethrow_exception(failure);
    }
    static void RemoveAdditions(Scene& scene,const Instance& instance) {
        RICE_BENCHMARK_FUNCTION();
        std::set<EntityID> original(instance.objects.begin(),instance.objects.end());
        std::vector<EntityID> additions;
        std::vector<GameObject*> roots;
        for(auto id:instance.objects)if(auto* object=scene.Find(id))roots.push_back(object);
        for(auto& object:scene.GetObjects())if(!original.contains(object->GetID()))
            for(auto* root:roots)if(object->DependsOn(root)){additions.push_back(object->GetID());break;}
        PrefabAsset::Destroy(scene,additions);
    }
public:
    explicit PrefabPool(ComponentRegistry& registry):registry(registry){RICE_BENCHMARK_FUNCTION();}
    EntityID Root(Scene& scene,EntityID member) {
        RICE_BENCHMARK_FUNCTION();Bind(scene);
        auto* object=scene.Find(member);
        if(!object)return 0;
        for(const auto& [root,instance]:instances)
            if(std::find(instance.objects.begin(),instance.objects.end(),member)!=instance.objects.end())
                return scene.Find(root)?root:0;
        for(const auto& [root,instance]:instances){
            auto* parent=scene.Find(root);
            if(parent&&object->DependsOn(parent))return root;
        }
        return 0;
    }
    size_t Available(Scene& scene,const std::shared_ptr<const PrefabAsset>& asset) {
        RICE_BENCHMARK_FUNCTION();Purge(scene);
        return std::count_if(instances.begin(),instances.end(),[&](const auto& pair){RICE_BENCHMARK_FUNCTION();return pair.second.asset==asset&&pair.second.available&&!pair.second.busy;});
    }
    void Prewarm(Scene& scene,const std::shared_ptr<const PrefabAsset>& asset,size_t count) {
        RICE_BENCHMARK_FUNCTION();
        if(!asset||!asset->ObjectCount()||count>10000/asset->ObjectCount())throw std::invalid_argument("Invalid prefab pool size");
        auto available=Available(scene,asset);
        for(;available<count;++available){
            auto objects=asset->Create(scene,registry,asset->OriginalPosition(),true);
            auto root=objects.front();
            try{instances.emplace(root,Instance{asset,objects,true});}
            catch(...){PrefabAsset::Destroy(scene,objects);throw;}
        }
    }
    GameObject& Instantiate(Scene& scene,const std::shared_ptr<const PrefabAsset>& asset,Vector3 position=Vector3(0)) {
        RICE_BENCHMARK_FUNCTION();
        if(!asset||!asset->ObjectCount())throw std::invalid_argument("Prefab asset is missing or empty");
        Bind(scene);
        auto found=instances.end();
        for(auto it=instances.begin();it!=instances.end();){
            if(!it->second.available||it->second.busy||it->second.asset!=asset){++it;continue;}
            if(Valid(scene,it->second)){found=it;break;}
            auto objects=std::move(it->second.objects);it=instances.erase(it);
            PrefabAsset::Destroy(scene,objects);
        }
        bool created=false;
        if(found==instances.end()){
            auto objects=asset->Create(scene,registry,position,true);auto root=objects.front();
            try{found=instances.emplace(root,Instance{asset,objects,true}).first;}
            catch(...){PrefabAsset::Destroy(scene,objects);throw;}
            created=true;
        }
        auto& instance=found->second;
        instance.available=false;instance.busy=true;
        try{
            if(!created){RemoveAdditions(scene,instance);asset->Restore(scene,instance.objects,registry,position,true);}
            // Restoration detaches the root on every acquire. Bind its game layer
            // before scripts observe the instance or activation makes it visible.
            if(auto* root=scene.Find(found->first))SceneLayers::PlaceRoot(*root);
            for(auto id:instance.objects){
                auto* object=scene.Find(id);
                if(!object)throw std::runtime_error("Prefab was destroyed by its acquire callback");
                object->NotifyPoolAcquire();
            }
            for(auto id:instance.objects){
                auto* object=scene.Find(id);
                if(!object)throw std::runtime_error("Prefab was destroyed by its acquire callback");
                object->SetPoolState(true,false);
            }
            for(auto id:instance.objects){
                auto* object=scene.Find(id);
                if(!object)throw std::runtime_error("Prefab was destroyed by its activation callback");
                object->SynchronizeActivation();
            }
            auto* root=scene.Find(found->first);
            if(!root)throw std::runtime_error("Prefab was destroyed by its activation callback");
            instance.busy=false;
            return *root;
        }catch(...){
            try{Deactivate(scene,instance,true);}catch(...){}
            auto objects=std::move(instance.objects);instances.erase(found);
            PrefabAsset::Destroy(scene,objects);throw;
        }
    }
    bool Release(Scene& scene,EntityID root) {
        RICE_BENCHMARK_FUNCTION();Bind(scene);
        auto found=instances.find(root);
        if(found==instances.end()||found->second.available||found->second.busy)return false;
        if(!Valid(scene,found->second)){
            auto objects=std::move(found->second.objects);instances.erase(found);
            PrefabAsset::Destroy(scene,objects);return false;
        }
        Deactivate(scene,found->second,true);return true;
    }
    void Clear(Scene* scene=nullptr) {
        RICE_BENCHMARK_FUNCTION();
        if(clearing)return;
        if(HasTransition())throw std::logic_error("Cannot clear a prefab pool from a lifecycle callback");
        clearing=true;
        if(scene&&scene->GetInstanceID()==sceneInstance){
            for(auto& [root,instance]:instances){
                if(!instance.available)try{Deactivate(*scene,instance,true);}catch(...){}
                for(auto id:instance.objects)scene->Destroy(id);
            }
            scene->FlushDestroyed();
        }
        instances.clear();sceneInstance=0;clearing=false;
    }
};

} // namespace Rice
