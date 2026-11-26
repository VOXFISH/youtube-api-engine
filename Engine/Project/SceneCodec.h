#pragma once
#include "Rice/Benchmark.h"
#include "Assets.h"
#include "Streaming/YouTubeListener.h"
#include <variant>
namespace Rice {
using Property=std::variant<bool*,int*,float*,std::string*,Vector2*,Vector3*,Vector4*,ShapeMesh*,EntityID*,TriangleMaterials*>;
using PropertyVisitor=std::function<void(const char*,Property)>;
struct ComponentType {
    std::string name;
    std::function<Component*(GameObject&)> get;
    std::function<Component&(GameObject&)> add;
    std::function<void(GameObject&)> remove;
    std::function<void(Component&,const PropertyVisitor&)> fields;
    bool inspectorVisible=true;
};
class ComponentRegistry {
public:
    std::vector<ComponentType> types;
    template<class T,class F>void Register(std::string name,F fields,bool inspectorVisible=true){
        RICE_BENCHMARK_FUNCTION();
        if(Find(name))throw std::invalid_argument("Duplicate component registration");
        types.push_back({std::move(name),[](GameObject& o)->Component*{ RICE_BENCHMARK_FUNCTION(); return o.GetComponent<T>();},[](GameObject& o)->Component&{ RICE_BENCHMARK_FUNCTION(); return o.AddComponent<T>();},[](GameObject& o){ RICE_BENCHMARK_FUNCTION(); o.RemoveComponent<T>();},[fields](Component& c,const PropertyVisitor& visit){ RICE_BENCHMARK_FUNCTION(); visit("Enabled",&c.enabled);fields(static_cast<T&>(c),visit);},inspectorVisible});
    }
    ComponentType* Find(const std::string& name){ RICE_BENCHMARK_FUNCTION(); for(auto& t:types)if(t.name==name)return &t;return nullptr;}
    ComponentRegistry(){
        RICE_BENCHMARK_FUNCTION();
        Register<SceneLayer>("Scene Layer",[](auto&,auto&){},false);
        Register<MeshRenderer>("MeshRenderer",[](auto& c,auto& f){ RICE_BENCHMARK_FUNCTION(); f("Color",&c.color);f("Shape",&c.shape);f("Custom mesh",&c.custom);f("Shape asset",&c.shapeAsset);f("Triangle materials",&c.triangleMaterials);f("Cast shadows",&c.castsShadow);});
        Register<CameraComponent>("Camera",[](auto& c,auto& f){f("Primary",&c.primary);f("Field of view",&c.fieldOfView);f("Near clip",&c.nearClip);f("Far clip",&c.farClip);});
        Register<CameraFollower>("Camera Follower",[](auto& c,auto& f){f("Camera",&c.camera);f("Translation",&c.transform.position);f("Rotation",&c.transform.rotation);f("Scale",&c.transform.localScale);f("Show in Shape Editor",&c.showInEditor);});
        Register<Anchor>("Anchor",[](auto& c,auto& f){f("Child",&c.child);f("Translation",&c.transform.position);f("Rotation",&c.transform.rotation);f("Scale",&c.transform.localScale);f("Show in Shape Editor",&c.showInEditor);});
        auto scriptFields = [](ScriptComponentBase& c, const PropertyVisitor& f) {
            f("Script file",&c.scriptFile);
            f("Child bindings",&c.childBindings);
            f("Child reads",&c.childReads);
            f("Data object key",&c.dataObjectKey);
        };
        Register<ObjectScript>("ObjectScript",scriptFields);
        Register<SingletonManager>("SingletonManager",scriptFields);
        Register<BlankScript>("BlankScript",scriptFields);
        Register<YouTubeListener>("YouTube Listener",[](auto& c,auto& f){f("Command bindings",&c.commandBindings);});
        Register<ReflectionProbe>("ReflectionProbe",[](auto& c,auto& f){
            RICE_BENCHMARK_FUNCTION();
            f("Cubemap resolution",&c.resolution);
            f("Mipmap storage quality",&c.storageQuality);
            f("Max object size",&c.maxObjectSize);
            f("Fix up edge seams",&c.fixEdgeSeams);
        });
        Register<Material>("Material",[](auto& c,auto& f){
            RICE_BENCHMARK_FUNCTION();
            f("Metalicity",&c.metallicity);
            f("Roughness",&c.roughness);
            f("Albedo asset",&c.albedoAsset);
        });
        Register<SpriteRenderer>("SpriteRenderer",[](auto& c,auto& f){ RICE_BENCHMARK_FUNCTION(); f("Color",&c.color);f("Image asset",&c.asset);f("Cast shadows",&c.castsShadow);});
        Register<Light>("Light",[](auto& c,auto& f){ RICE_BENCHMARK_FUNCTION(); f("Color",&c.color);f("Intensity",&c.intensity);f("Direction",&c.direction);f("Type",&c.type);f("Range",&c.range);f("Cast shadows",&c.castsShadow);});
        Register<BoxCollider>("BoxCollider",[](auto& c,auto& f){ RICE_BENCHMARK_FUNCTION(); f("Center",&c.center);f("Size",&c.size);f("Trigger",&c.isTrigger);});
        Register<Rigidbody>("Rigidbody",[](auto& c,auto& f){ RICE_BENCHMARK_FUNCTION(); f("Velocity",&c.velocity);f("Mass",&c.mass);f("Restitution",&c.restitution);f("Gravity",&c.useGravity);f("Kinematic",&c.kinematic);});
        Register<SpriteAnimator>("SpriteAnimator",[](auto& c,auto& f){ RICE_BENCHMARK_FUNCTION(); f("Columns",&c.columns);f("Rows",&c.rows);f("Frames",&c.frames);f("FPS",&c.fps);f("Loop",&c.loop);f("Playing",&c.playing);});
        Register<AudioSource>("AudioSource",[](auto& c,auto& f){ RICE_BENCHMARK_FUNCTION(); f("Audio asset",&c.clip);f("Loop",&c.loop);f("Play on start",&c.playOnStart);f("Volume",&c.volume);});
        Register<UIText>("UIText",[](auto& c,auto& f){ RICE_BENCHMARK_FUNCTION(); f("Text",&c.text);f("Position",&c.position);f("Color",&c.color);f("Font size",&c.size);});
        Register<UIButton>("UIButton",[](auto& c,auto& f){ RICE_BENCHMARK_FUNCTION(); f("Text",&c.text);f("Position",&c.position);f("Size",&c.size);f("Action",&c.action);});
        Register<PlayerController>("PlayerController",[](auto& c,auto& f){ RICE_BENCHMARK_FUNCTION(); f("Speed",&c.speed);});
    }
};
inline void WriteProperty(std::ostream& out,Property property){ RICE_BENCHMARK_FUNCTION(); std::visit([&](auto* value){ RICE_BENCHMARK_FUNCTION(); using T=std::remove_cvref_t<decltype(*value)>;if constexpr(std::is_same_v<T,ShapeMesh>||std::is_same_v<T,TriangleMaterials>)value->Write(out);else if constexpr(std::is_same_v<T,std::string>)out<<std::quoted(*value);else if constexpr(std::is_arithmetic_v<T>)out<<*value;else for(int i=0;i<value->length();++i)out<<(*value)[i]<<' ';},property);}
inline void ReadProperty(std::istream& in,Property property){ RICE_BENCHMARK_FUNCTION(); std::visit([&](auto* value){ RICE_BENCHMARK_FUNCTION(); using T=std::remove_cvref_t<decltype(*value)>;if constexpr(std::is_same_v<T,ShapeMesh>||std::is_same_v<T,TriangleMaterials>)value->Read(in);else if constexpr(std::is_same_v<T,std::string>){in>>std::quoted(*value);if(value->size()>65536)throw std::runtime_error("Property too long");}else if constexpr(std::is_arithmetic_v<T>){in>>*value;if constexpr(std::is_floating_point_v<T>)if(!std::isfinite(*value))throw std::runtime_error("Nonfinite property");}else for(int i=0;i<value->length();++i){in>>(*value)[i];if(!std::isfinite((*value)[i]))throw std::runtime_error("Nonfinite vector");}},property);if(!in)throw std::runtime_error("Invalid property");}
// Both clipboard copies and prefabs keep registered values without runtime handles.
struct ComponentSnapshot {
    std::string type,data;

    static ComponentSnapshot Capture(ComponentType& type,Component& component) {
        RICE_BENCHMARK_FUNCTION();
        std::ostringstream output;output<<std::setprecision(9);
        type.fields(component,[&](const char*,Property property){WriteProperty(output,property);output<<'\n';});
        return {type.name,output.str()};
    }
    static std::vector<ComponentSnapshot> CaptureAll(GameObject& object,ComponentRegistry& registry) {
        RICE_BENCHMARK_FUNCTION();
        std::vector<ComponentSnapshot> values;size_t count=0;
        object.EachComponent([&](Component&){++count;});
        for(auto& type:registry.types)if(auto* component=type.get(object))values.push_back(Capture(type,*component));
        if(values.size()!=count)throw std::runtime_error("Register all component fields before copying");
        return values;
    }
    static void Apply(GameObject& object,const std::vector<ComponentSnapshot>& values,ComponentRegistry& registry) {
        RICE_BENCHMARK_FUNCTION();
        for(const auto& value:values){
            auto* type=registry.Find(value.type);
            if(!type)throw std::runtime_error("Component is not registered: "+value.type);
            auto& component=type->add(object);std::istringstream input(value.data);
            type->fields(component,[&](const char* key,Property property){
                if(std::string_view(key)=="Cast shadows"){
                    if(!input.eof())input>>std::ws;
                    if(input.eof()){*std::get<bool*>(property)=true;return;}
                }
                if(dynamic_cast<ScriptComponentBase*>(&component)&&std::holds_alternative<std::string*>(property)){
                    input>>std::ws;
                    if(input.eof()){
                        if(std::string_view(key)!="Data object key")std::get<std::string*>(property)->clear();
                        return;
                    }
                }
                if(type->name=="MeshRenderer"&&std::string_view(key)=="Shape asset"){
                    if(input.eof()){std::get<std::string*>(property)->clear();return;}input>>std::ws;if(input.eof()){std::get<std::string*>(property)->clear();return;}
                }
                if(std::holds_alternative<TriangleMaterials*>(property)){
                    if(!input.eof())input>>std::ws;
                    if(input.eof()){std::get<TriangleMaterials*>(property)->faces.clear();return;}
                }
                // Older prefab/clipboard payloads end after Shape.
                if(std::holds_alternative<ShapeMesh*>(property)){
                    input>>std::ws;if(input.eof()){auto* mesh=std::get<ShapeMesh*>(property);auto revision=mesh->revision;*mesh=ShapeMesh{};mesh->revision=revision+1;return;}
                }
                ReadProperty(input,property);
            });
            input>>std::ws;
            if(!input.eof())throw std::runtime_error("Unexpected component properties: "+value.type);
            if(auto* mesh=object.GetComponent<MeshRenderer>())mesh->triangleMaterials.Validate(mesh->custom.indices.size()/3);
        }
    }
    static void Restore(GameObject& object,const std::vector<ComponentSnapshot>& values,ComponentRegistry& registry) {
        RICE_BENCHMARK_FUNCTION();
        std::set<Component*> retained;
        for(const auto& value:values){
            auto* type=registry.Find(value.type);
            if(!type)throw std::runtime_error("Component is not registered: "+value.type);
            if(auto* component=type->get(object))retained.insert(component);
        }
        object.EachComponent([&](Component& component){if(!retained.contains(&component))object.RemoveComponent(component);});
        object.FlushComponents();
        Apply(object,values,registry);
    }
};
inline void WriteMatrix(std::ostream& out,const Matrix4& value){ RICE_BENCHMARK_FUNCTION(); for(int c=0;c<4;++c)for(int r=0;r<4;++r)out<<value[c][r]<<' ';}
inline void ReadMatrix(std::istream& in,Matrix4& value){ RICE_BENCHMARK_FUNCTION(); for(int c=0;c<4;++c)for(int r=0;r<4;++r){in>>value[c][r];if(!in||!std::isfinite(value[c][r]))throw std::runtime_error("Invalid matrix");}}
class SceneCodec {
    ComponentRegistry& registry;
    static void Expect(std::istream& in,const std::string& expected){ RICE_BENCHMARK_FUNCTION(); std::string token;in>>std::quoted(token);if(!in||token!=expected)throw std::runtime_error("Expected "+expected);}
public:
    explicit SceneCodec(ComponentRegistry& r):registry(r){ RICE_BENCHMARK_FUNCTION(); }
    std::string Encode(Scene& scene,bool editorSettings=true){ RICE_BENCHMARK_FUNCTION(); std::ostringstream out;out<<std::setprecision(9)<<"RICE_SCENE 5\n";
        auto& settings=scene.settings;out<<"Settings "<<settings.skybox<<' '<<std::quoted(settings.skyboxAsset)<<' '<<settings.directionalLight<<' ';
        WriteProperty(out,&settings.lightColor);out<<' '<<settings.lightIntensity<<' ';WriteProperty(out,&settings.lightDirection);
        out<<' '<<settings.ground<<' '<<settings.groundCollision<<'\n';
        if(editorSettings){out<<"EditorGrid ";WriteProperty(out,&settings.axes);WriteProperty(out,&settings.grids);out<<'\n';}
        for(auto& obj:scene.GetObjects())if(!obj->IsPendingDestroy()&&!obj->IsPoolAvailable()){
            size_t attached=0,registered=0;obj->EachComponent([&](Component&){ RICE_BENCHMARK_FUNCTION(); ++attached;});for(auto& t:registry.types)if(t.get(*obj))++registered;if(attached!=registered)throw std::runtime_error("Register custom component fields before saving/playing");
            out<<"Object "<<obj->GetID()<<' '<<(obj->GetParent()?obj->GetParent()->GetID():0)<<' '<<std::quoted(obj->name)<<' '<<obj->activeSelf<<' ';
            WriteProperty(out,&obj->transform.position);WriteProperty(out,&obj->transform.rotation);WriteProperty(out,&obj->transform.localScale);out<<obj->isGroup<<' '<<(obj->GetGroup()?obj->GetGroup()->GetID():0)<<' '<<obj->previousGroup<<' ';WriteMatrix(out,obj->transform.affine);WriteMatrix(out,obj->groupOffset);WriteMatrix(out,obj->groupParentBind);WriteMatrix(out,obj->groupCommonBind);WriteProperty(out,&obj->transform.pivot);out<<'\n';
            for(auto& type:registry.types)if(auto* c=type.get(*obj)){out<<"Component "<<std::quoted(type.name)<<'\n';type.fields(*c,[&](const char* key,Property p){ RICE_BENCHMARK_FUNCTION(); out<<std::quoted(key)<<' ';WriteProperty(out,p);out<<'\n';});out<<"EndComponent\n";}
            out<<"EndObject\n";
        }out<<"EndScene\n";return out.str();}
    Scope<Scene> Decode(const std::string& text){ RICE_BENCHMARK_FUNCTION(); if(text.size()>16*1024*1024)throw std::runtime_error("Scene exceeds 16 MiB");std::istringstream in(text);Expect(in,"RICE_SCENE");int version=0;in>>version;if(version<1||version>5)throw std::runtime_error("Unsupported scene version");auto result=CreateScope<Scene>();std::vector<std::pair<EntityID,EntityID>> parents,groups;
        if(version>=5){
            Expect(in,"Settings");auto& settings=result->settings;
            ReadProperty(in,&settings.skybox);ReadProperty(in,&settings.skyboxAsset);ReadProperty(in,&settings.directionalLight);
            ReadProperty(in,&settings.lightColor);ReadProperty(in,&settings.lightIntensity);ReadProperty(in,&settings.lightDirection);
            ReadProperty(in,&settings.ground);ReadProperty(in,&settings.groundCollision);
        }
        bool gridRead=false;
        std::string token;while(in>>token){if(token=="EndScene")break;
            if(token=="EditorGrid"&&version>=5&&!gridRead&&result->GetObjectCount()==0){ReadProperty(in,&result->settings.axes);ReadProperty(in,&result->settings.grids);gridRead=true;continue;}if(token!="Object")throw std::runtime_error("Expected Object");EntityID id=0,parent=0;std::string name;bool active=true;in>>id>>parent>>std::quoted(name)>>active;if(!in||id==0||id==UINT64_MAX||result->GetObjectCount()>=10000)throw std::runtime_error("Invalid entity");auto& obj=result->CreateGameObject(name,id);obj.activeSelf=active;ReadProperty(in,&obj.transform.position);ReadProperty(in,&obj.transform.rotation);ReadProperty(in,&obj.transform.localScale);if(version>=2){EntityID group=0;in>>obj.isGroup>>group>>obj.previousGroup;ReadMatrix(in,obj.transform.affine);ReadMatrix(in,obj.groupOffset);ReadMatrix(in,obj.groupParentBind);ReadMatrix(in,obj.groupCommonBind);groups.push_back({id,group});}parents.push_back({id,parent});std::set<std::string> found;
            if(version>=4)ReadProperty(in,&obj.transform.pivot);
            while(in>>token){if(token=="EndObject")break;if(token!="Component")throw std::runtime_error("Expected Component");std::string typeName;in>>std::quoted(typeName);auto* type=registry.Find(typeName);if(!type||!found.insert(typeName).second)throw std::runtime_error("Unknown/duplicate component: "+typeName);auto& c=type->add(obj);type->fields(c,[&](const char* key,Property p){ RICE_BENCHMARK_FUNCTION(); if(version<3&&typeName=="Light"&&(std::string(key)=="Type"||std::string(key)=="Range"))return;
                if(typeName=="MeshRenderer" && (std::string_view(key)=="Shape"||std::string_view(key)=="Custom mesh"||std::string_view(key)=="Shape asset"||std::string_view(key)=="Triangle materials")){
                    auto position=in.tellg();
                    std::string next;
                    in>>std::quoted(next);
                    if(!in)throw std::runtime_error("Truncated MeshRenderer");
                    in.seekg(position);
                    // Scenes saved before basic shapes contain only Enabled and Color.
                    if(next=="EndComponent"||next=="Cast shadows"||(std::string_view(key)=="Shape"&&next=="Custom mesh")||(std::string_view(key)=="Custom mesh"&&next=="Shape asset")||(std::string_view(key)=="Shape asset"&&next=="Triangle materials"))return;
                }
                if(dynamic_cast<ScriptComponentBase*>(&c)&&std::holds_alternative<std::string*>(p)){
                    auto position=in.tellg();std::string next;in>>std::quoted(next);
                    if(!in)throw std::runtime_error("Truncated script component");
                    in.seekg(position);if(next!=key)return;
                }
                if(std::string_view(key)=="Cast shadows"){
                    auto position=in.tellg();std::string next;in>>std::quoted(next);
                    if(!in)throw std::runtime_error("Truncated shadow setting");
                    in.seekg(position);if(next=="EndComponent")return;
                }
                Expect(in,key);ReadProperty(in,p);});Expect(in,"EndComponent");}if(token!="EndObject")throw std::runtime_error("Truncated object");
            // Older transforms used the mesh origin. Adopt the centered pivot
            // without moving existing, possibly deformed, scene geometry.
            if(version<4){auto pivot=obj.GetLocalPivot();obj.transform.position+=Vector3(obj.transform.GetMatrix()*Vector4(pivot,0))-pivot;}
            if(auto* mesh=obj.GetComponent<MeshRenderer>())mesh->triangleMaterials.Validate(mesh->custom.indices.size()/3);
        }if(token!="EndScene")throw std::runtime_error("Truncated scene");in>>std::ws;if(!in.eof())throw std::runtime_error("Trailing scene data");
        for(auto [id,parent]:parents)if(parent){auto* target=result->Find(parent);if(!target)throw std::runtime_error("Missing parent");result->Find(id)->SetParent(target);}
        for(auto [id,group]:groups)if(group){auto* target=result->Find(group);if(!target)throw std::runtime_error("Missing group");auto* obj=result->Find(id);auto offset=obj->groupOffset,bind=obj->groupParentBind,common=obj->groupCommonBind;obj->SetGroup(target,false);obj->groupOffset=offset;obj->groupParentBind=bind;obj->groupCommonBind=common;}return result;
    }
    void Save(Scene& scene,const fs::path& path){ RICE_BENCHMARK_FUNCTION(); AtomicWrite(path,Encode(scene));}
    Scope<Scene> Load(const fs::path& path){ RICE_BENCHMARK_FUNCTION(); return Decode(ReadFile(path));}
    GameObject& Duplicate(Scene& scene,EntityID id){ RICE_BENCHMARK_FUNCTION(); auto* original=scene.Find(id);if(!original)throw std::runtime_error("Missing entity");if(original->isGroup){
            std::vector<GameObject*> sources{original};
            for(auto& obj:scene.GetObjects())if(obj.get()!=original&&!obj->IsPendingDestroy())for(auto* p=obj->GetHierarchyParent();p;p=p->GetHierarchyParent())if(p==original){sources.push_back(obj.get());break;}
            std::map<EntityID,GameObject*> copies;
            for(auto* source:sources){auto& copy=scene.CreateGameObject(source->name+(source==original?" Copy":""));copies[source->GetID()]=&copy;copy.transform=source->transform;copy.activeSelf=source->activeSelf;copy.isGroup=source->isGroup;
                for(auto& t:registry.types)if(auto* c=t.get(*source)){std::ostringstream out;out<<std::setprecision(9);t.fields(*c,[&](const char*,Property p){ RICE_BENCHMARK_FUNCTION(); WriteProperty(out,p);out<<'\n';});std::istringstream in(out.str());auto& target=t.add(copy);t.fields(target,[&](const char*,Property p){ RICE_BENCHMARK_FUNCTION(); ReadProperty(in,p);});}}
            auto mapped=[&](GameObject* obj){ RICE_BENCHMARK_FUNCTION(); return obj&&copies.contains(obj->GetID())?copies.at(obj->GetID()):obj;};
            for(auto& [source,copy]:copies)RemapScriptBindings(*copy,[&](EntityID id){return copies.contains(id)?copies.at(id)->GetID():EntityID{};});
            for(auto& [source,copy]:copies){(void)source;if(auto* a=copy->GetComponent<Anchor>())a->child=copies.contains(a->child)?copies.at(a->child)->GetID():0;}
            for(auto& [source,copy]:copies)if(auto* f=copy->GetComponent<CameraFollower>();f&&copies.contains(f->camera))f->camera=copies.at(f->camera)->GetID();
            for(auto* source:sources)copies.at(source->GetID())->SetParent(mapped(source->GetParent()));
            for(auto* source:sources){auto* copy=copies.at(source->GetID());if(source->GetGroup()){copy->SetGroup(mapped(source->GetGroup()),false);copy->groupOffset=source->groupOffset;copy->groupParentBind=source->groupParentBind;copy->groupCommonBind=source->groupCommonBind;}copy->previousGroup=copies.contains(source->previousGroup)?copies.at(source->previousGroup)->GetID():source->previousGroup;}
            return *copies.at(id);
        }
        auto& copy=scene.CreateGameObject(original->name+" Copy");copy.transform=original->transform;copy.activeSelf=original->activeSelf;copy.SetParent(original->GetParent());
        for(auto& t:registry.types)if(auto* source=t.get(*original)){std::ostringstream out;t.fields(*source,[&](const char*,Property p){ RICE_BENCHMARK_FUNCTION(); WriteProperty(out,p);out<<'\n';});std::istringstream in(out.str());auto& target=t.add(copy);t.fields(target,[&](const char*,Property p){ RICE_BENCHMARK_FUNCTION(); ReadProperty(in,p);});}
        if(auto* anchor=copy.GetComponent<Anchor>())anchor->child=0;
        return copy;}
};
class EditorSession {
    // Editor history keeps registered values in their native types. There is no
    // text conversion while dragging, and unchanged objects never enter history.
    using PropertyValue=std::variant<bool,int,float,std::string,Vector2,Vector3,Vector4,ShapeMesh,EntityID,TriangleMaterials>;
    struct FieldState {std::string name;PropertyValue value;};
    struct ComponentState {std::string type;std::vector<FieldState> fields;};
    struct ObjectState {
        EntityID id=0,parent=0,group=0,previousGroup=0;
        std::string name;bool active=true,isGroup=false;
        Transform transform;Matrix4 offset{1},bind{1},common{1};
        std::vector<ComponentState> components;
    };
public:
    struct EditSnapshot {
    private:
        friend class EditorSession;
        uint64_t instance=0,generation=0;
        bool all=false;
        std::map<EntityID,std::optional<ObjectState>> objects;
        std::optional<SceneSettings> settings;
        std::vector<EntityID> order;
    };
private:
    struct ObjectChange {EntityID id=0;std::optional<ObjectState> before,after;};
    struct HistoryEntry {
        std::vector<ObjectChange> objects;
        std::optional<SceneSettings> beforeSettings,afterSettings;
        std::vector<EntityID> beforeOrder,afterOrder;
        // Compatibility for explicit callers of Snapshot()/Record(string).
        // Keep their text opaque until Undo/Redo: recording must never invoke
        // custom component Awake/OnDestroy by decoding a temporary scene.
        std::string beforeText,afterText;
    };
    ComponentRegistry& registry;
    std::vector<HistoryEntry> undo,redo;
    std::string editSnapshot;
    uint64_t historyGeneration=0,snapshotCaptureCount=0,capturedObjectCount=0;

    static bool SameValue(const PropertyValue& a,const PropertyValue& b){
        if(a.index()!=b.index())return false;
        return std::visit([&](const auto& value){
            using T=std::remove_cvref_t<decltype(value)>;const auto& other=std::get<T>(b);
            if constexpr(std::is_same_v<T,TriangleMaterials>)return value.faces==other.faces;
            else return value==other;
        },a);
    }
    static bool SameTransform(const Transform& a,const Transform& b){return a.position==b.position&&a.rotation==b.rotation&&a.localScale==b.localScale&&a.pivot==b.pivot&&a.affine==b.affine;}
    static bool SameObject(const ObjectState& a,const ObjectState& b){
        if(a.id!=b.id||a.parent!=b.parent||a.group!=b.group||a.previousGroup!=b.previousGroup||a.name!=b.name||a.active!=b.active||a.isGroup!=b.isGroup||
           !SameTransform(a.transform,b.transform)||a.offset!=b.offset||a.bind!=b.bind||a.common!=b.common||a.components.size()!=b.components.size())return false;
        for(size_t i=0;i<a.components.size();++i){
            const auto& first=a.components[i];const auto& second=b.components[i];
            if(first.type!=second.type||first.fields.size()!=second.fields.size())return false;
            for(size_t j=0;j<first.fields.size();++j)if(first.fields[j].name!=second.fields[j].name||!SameValue(first.fields[j].value,second.fields[j].value))return false;
        }
        return true;
    }
    static bool SameSettings(const SceneSettings& a,const SceneSettings& b){
        return a.skybox==b.skybox&&a.skyboxAsset==b.skyboxAsset&&a.directionalLight==b.directionalLight&&a.lightColor==b.lightColor&&
               a.lightIntensity==b.lightIntensity&&a.lightDirection==b.lightDirection&&a.ground==b.ground&&a.groundCollision==b.groundCollision&&a.axes==b.axes&&a.grids==b.grids;
    }
    ObjectState CaptureState(GameObject& object){
        RICE_BENCHMARK_FUNCTION();
        ++capturedObjectCount;
        ObjectState result;result.id=object.GetID();result.parent=object.GetParent()?object.GetParent()->GetID():0;result.group=object.GetGroup()?object.GetGroup()->GetID():0;
        result.previousGroup=object.previousGroup;result.name=object.name;result.active=object.activeSelf;result.isGroup=object.isGroup;
        result.transform=object.transform;result.offset=object.groupOffset;result.bind=object.groupParentBind;result.common=object.groupCommonBind;
        size_t attached=0;object.EachComponent([&](Component&){++attached;});
        for(auto& type:registry.types)if(auto* component=type.get(object)){
            ComponentState state;state.type=type.name;
            type.fields(*component,[&](const char* name,Property property){
                PropertyValue value=std::visit([](auto* source)->PropertyValue {
                    using T=std::remove_cvref_t<decltype(*source)>;
                    if constexpr(std::is_same_v<T,TriangleMaterials>){
                        auto copy=*source;for(auto& [index,material]:copy.faces){material.albedoTexture.reset();material.loadedAsset.clear();}return copy;
                    }else return *source;
                },property);
                state.fields.push_back({name,std::move(value)});
            });
            result.components.push_back(std::move(state));
        }
        if(attached!=result.components.size())throw std::runtime_error("Register custom component fields before editing");
        return result;
    }
    EditSnapshot CaptureScene(Scene& source){
        RICE_BENCHMARK_FUNCTION();
        ++snapshotCaptureCount;EditSnapshot result;result.instance=source.GetInstanceID();result.generation=historyGeneration;result.all=true;result.settings=source.settings;
        for(auto& object:source.GetObjects())if(!object->IsPendingDestroy()&&!object->IsPoolAvailable()){
            auto state=CaptureState(*object);result.order.push_back(state.id);result.objects.emplace(state.id,std::move(state));
        }
        return result;
    }
    void Commit(const EditSnapshot& before,const EditSnapshot& after){
        RICE_BENCHMARK_FUNCTION();
        HistoryEntry entry;
        auto add=[&](EntityID id,const std::optional<ObjectState>& first,const std::optional<ObjectState>& second){
            if(bool(first)==bool(second)&&(!first||SameObject(*first,*second)))return;
            entry.objects.push_back({id,first,second});
        };
        for(const auto& [id,state]:before.objects){auto found=after.objects.find(id);add(id,state,found==after.objects.end()?std::nullopt:found->second);}
        for(const auto& [id,state]:after.objects)if(!before.objects.contains(id))add(id,std::nullopt,state);
        if(before.settings&&after.settings&&!SameSettings(*before.settings,*after.settings)){entry.beforeSettings=before.settings;entry.afterSettings=after.settings;}
        if(before.all&&before.order!=after.order){entry.beforeOrder=before.order;entry.afterOrder=after.order;}
        if(entry.objects.empty()&&!entry.beforeSettings&&entry.beforeOrder.empty()&&entry.afterOrder.empty())return;
        undo.push_back(std::move(entry));if(undo.size()>100)undo.erase(undo.begin());redo.clear();++historyGeneration;
    }
    void RestoreComponents(GameObject& object,const ObjectState& state){
        RICE_BENCHMARK_FUNCTION();
        std::set<Component*> retained;
        for(const auto& component:state.components){
            auto* type=registry.Find(component.type);if(!type)throw std::runtime_error("Component is not registered: "+component.type);
            if(auto* current=type->get(object))retained.insert(current);
        }
        object.EachComponent([&](Component& component){if(!retained.contains(&component))object.RemoveComponent(component);});object.FlushComponents();
        for(const auto& saved:state.components){
            auto& type=*registry.Find(saved.type);auto& component=type.add(object);size_t field=0;
            type.fields(component,[&](const char* name,Property property){
                if(field>=saved.fields.size()||saved.fields[field].name!=name)throw std::runtime_error("Component registration changed during undo: "+saved.type);
                const auto& value=saved.fields[field++].value;
                std::visit([&](auto* target){
                    using T=std::remove_cvref_t<decltype(*target)>;const auto& source=std::get<T>(value);
                    if constexpr(std::is_same_v<T,ShapeMesh>){
                        if(*target!=source){auto revision=target->revision;*target=source;target->revision=revision+1;}
                    }else if constexpr(std::is_same_v<T,TriangleMaterials>){if(target->faces!=source.faces)*target=source;}
                    else *target=source;
                },property);
            });
            if(field!=saved.fields.size())throw std::runtime_error("Component registration changed during undo: "+saved.type);
        }
    }
    void ValidateApply(const HistoryEntry& entry,bool forward){
        RICE_BENCHMARK_FUNCTION();
        struct Links {EntityID parent=0,group=0;bool isGroup=false;};
        std::map<EntityID,Links> graph;
        for(auto& object:scene->GetObjects())if(!object->IsPendingDestroy())
            graph.emplace(object->GetID(),Links{object->GetParent()?object->GetParent()->GetID():0,object->GetGroup()?object->GetGroup()->GetID():0,object->isGroup});
        for(const auto& change:entry.objects){
            const auto& target=forward?change.after:change.before;
            if(!target){graph.erase(change.id);continue;}
            graph[change.id]={target->parent,target->group,target->isGroup};
            for(const auto& saved:target->components){
                auto* type=registry.Find(saved.type);if(!type)throw std::runtime_error("Component is not registered: "+saved.type);
                if(auto* object=scene->Find(change.id))if(auto* component=type->get(*object)){
                    size_t index=0;
                    type->fields(*component,[&](const char* name,Property property){
                        if(index>=saved.fields.size()||saved.fields[index].name!=name||saved.fields[index].value.index()!=property.index())
                            throw std::runtime_error("Component registration changed during undo: "+saved.type);
                        ++index;
                    });
                    if(index!=saved.fields.size())throw std::runtime_error("Component registration changed during undo: "+saved.type);
                }
            }
        }
        std::map<EntityID,int> visited;
        std::function<void(EntityID)> visit=[&](EntityID id){
            if(!id)return;auto found=graph.find(id);if(found==graph.end())throw std::runtime_error("Missing undo hierarchy reference");
            auto& status=visited[id];if(status==2)return;if(status==1)throw std::runtime_error("Cyclic undo hierarchy");status=1;
            visit(found->second.parent);visit(found->second.group);
            if(found->second.group&&!graph.at(found->second.group).isGroup)throw std::runtime_error("Undo group is not a group object");status=2;
        };
        for(const auto& [id,links]:graph)visit(id);
    }
    void ApplyValues(const HistoryEntry& entry,bool forward){
        RICE_BENCHMARK_FUNCTION();
        std::map<EntityID,const ObjectState*> targets;
        std::map<EntityID,EntityID> incidentalAnchors;
        for(const auto& change:entry.objects){const auto& value=forward?change.after:change.before;targets.emplace(change.id,value?&*value:nullptr);}
        // Detach changed relationships first, so restoring a hierarchy never
        // creates a temporary cycle or cascades a deletion into surviving nodes.
        for(const auto& [id,target]:targets)if(auto* object=scene->Find(id)){
            auto* parent=object->GetParent();auto* group=object->GetGroup();
            if(parent&&(!target||target->parent!=parent->GetID())){
                if(!targets.contains(parent->GetID()))if(auto* anchor=parent->GetComponent<Anchor>())incidentalAnchors.try_emplace(parent->GetID(),anchor->child);
                object->SetParent(nullptr);
            }
            if(group&&(!target||target->group!=group->GetID()))object->SetGroup(nullptr,false);
        }
        for(const auto& [id,target]:targets)if(!target)scene->Destroy(id);
        scene->FlushDestroyed();
        for(const auto& [id,target]:targets)if(target){
            auto* object=scene->Find(id);if(!object)object=&scene->CreateGameObject(target->name,id);
            object->name=target->name;object->activeSelf=target->active;object->isGroup=target->isGroup;object->previousGroup=target->previousGroup;
            object->transform=target->transform;object->groupOffset=target->offset;object->groupParentBind=target->bind;object->groupCommonBind=target->common;
        }
        for(const auto& [id,target]:targets)if(target){
            auto& object=*scene->Find(id);auto* parent=target->parent?scene->Find(target->parent):nullptr;
            if(target->parent&&!parent)throw std::runtime_error("Missing undo parent");
            if(object.GetParent()!=parent)object.SetParent(parent);
        }
        // Resolve groups in dependency order; SetGroup reads ancestor matrices.
        std::set<EntityID> pending;for(const auto& [id,target]:targets)if(target)pending.insert(id);
        while(!pending.empty()){
            bool progress=false;
            for(auto it=pending.begin();it!=pending.end();){
                const auto& target=*targets.at(*it);
                if((target.parent&&pending.contains(target.parent))||(target.group&&pending.contains(target.group))){++it;continue;}
                auto& object=*scene->Find(*it);auto* group=target.group?scene->Find(target.group):nullptr;
                if(target.group&&!group)throw std::runtime_error("Missing undo group");
                if(object.GetGroup()!=group)object.SetGroup(group,false);
                object.groupOffset=target.offset;object.groupParentBind=target.bind;object.groupCommonBind=target.common;
                it=pending.erase(it);progress=true;
            }
            if(!progress)throw std::runtime_error("Cyclic undo hierarchy");
        }
        for(const auto& [id,child]:incidentalAnchors)if(auto* object=scene->Find(id))if(auto* anchor=object->GetComponent<Anchor>())anchor->child=child;
        // Parent changes can clear anchors. Restore registered fields last.
        for(const auto& [id,target]:targets)if(target)RestoreComponents(*scene->Find(id),*target);
        if(const auto& settings=forward?entry.afterSettings:entry.beforeSettings;settings)scene->settings=*settings;
        const auto& order=forward?entry.afterOrder:entry.beforeOrder;
        if(!entry.beforeOrder.empty()||!entry.afterOrder.empty())scene->RestoreObjectOrder(order);
    }
    void Apply(const HistoryEntry& entry,bool forward){
        RICE_BENCHMARK_FUNCTION();
        if(!entry.beforeText.empty()){
            auto restored=SceneCodec(registry).Decode(forward?entry.afterText:entry.beforeText);
            scene=std::move(restored);++previewRevision;++historyRevision;++historyGeneration;SyncSelection();return;
        }
        ValidateApply(entry,forward);
        // Validate before changing anything, and retain the touched values for
        // rollback if a user component lifecycle callback throws during apply.
        HistoryEntry recovery;
        for(const auto& change:entry.objects){
            auto* object=scene->Find(change.id);
            recovery.objects.push_back({change.id,std::nullopt,object?std::optional<ObjectState>(CaptureState(*object)):std::nullopt});
        }
        if(entry.beforeSettings||entry.afterSettings)recovery.afterSettings=scene->settings;
        if(!entry.beforeOrder.empty()||!entry.afterOrder.empty()){
            for(auto& object:scene->GetObjects())if(!object->IsPendingDestroy())recovery.afterOrder.push_back(object->GetID());
            recovery.beforeOrder=recovery.afterOrder;
        }
        try{ApplyValues(entry,forward);}
        catch(...){auto failure=std::current_exception();try{ApplyValues(recovery,true);}catch(...){}std::rethrow_exception(failure);}
        ++previewRevision;++historyRevision;++historyGeneration;SyncSelection();
    }
public:
    Scope<Scene> scene=CreateScope<Scene>();bool playing=false,paused=false;EntityID selected=0;
    explicit EditorSession(ComponentRegistry& r):registry(r){ RICE_BENCHMARK_FUNCTION(); }
    std::string Snapshot() const { RICE_BENCHMARK_FUNCTION(); return SceneCodec(registry).Encode(*scene);}
    std::string DocumentSnapshot() const {
        RICE_BENCHMARK_FUNCTION();
        return playing ? editSnapshot : Snapshot();
    }
    // Call only when an edit is activated, before applying the first changed
    // value. Structural commands use CaptureAll/Edit to include side effects.
    EditSnapshot CaptureAll(){RICE_BENCHMARK_FUNCTION();return CaptureScene(*scene);}
    EditSnapshot CaptureObjects(const std::vector<EntityID>& ids){
        RICE_BENCHMARK_FUNCTION();
        ++snapshotCaptureCount;EditSnapshot result;result.instance=scene->GetInstanceID();result.generation=historyGeneration;
        for(auto id:ids)if(!result.objects.contains(id)){
            auto* object=scene->Find(id);
            if(object&&!object->IsPoolAvailable())result.objects.emplace(id,CaptureState(*object));else result.objects.emplace(id,std::nullopt);
        }
        return result;
    }
    EditSnapshot CaptureObject(EntityID id){RICE_BENCHMARK_FUNCTION();return CaptureObjects({id});}
    EditSnapshot CaptureSettings(){RICE_BENCHMARK_FUNCTION();++snapshotCaptureCount;EditSnapshot result;result.instance=scene->GetInstanceID();result.generation=historyGeneration;result.settings=scene->settings;return result;}
    void Record(const EditSnapshot& before){
        RICE_BENCHMARK_FUNCTION();
        if(playing||before.instance!=scene->GetInstanceID()||before.generation!=historyGeneration)return;
        EditSnapshot after;
        if(before.all)after=CaptureAll();else{
            std::vector<EntityID> ids;for(const auto& [id,state]:before.objects)ids.push_back(id);
            after=CaptureObjects(ids);if(before.settings)after.settings=scene->settings;
        }
        Commit(before,after);
    }
    size_t UndoCount()const{return undo.size();}
    size_t RedoCount()const{return redo.size();}
    uint64_t SnapshotCaptureCount()const{return snapshotCaptureCount;}
    uint64_t CapturedObjectCount()const{return capturedObjectCount;}
    size_t HistoryObjectCount()const{size_t count=0;for(const auto& entry:undo)count+=entry.objects.size();for(const auto& entry:redo)count+=entry.objects.size();return count;}
    uint64_t previewRevision=0;
    uint64_t documentRevision=0;
    uint64_t historyRevision=0; // In-place Undo/Redo invalidates editor caches.
    bool PreviewTransform(EntityID id,const Transform& value){
        RICE_BENCHMARK_FUNCTION();
        if(playing)return false;
        auto* obj=scene->Find(id);if(!obj)return false;
        obj->transform=value;++previewRevision;return true;
    }

    std::set<EntityID> selection;
    void SyncSelection(){
        RICE_BENCHMARK_FUNCTION();
        std::erase_if(selection,[&](EntityID id){ RICE_BENCHMARK_FUNCTION(); return !scene->Find(id);});
        if(!scene->Find(selected))selected=0;
        if(selected&&!selection.contains(selected)){selection.clear();selection.insert(selected);}
        if(!selected&&!selection.empty())selected=*selection.rbegin();
    }
    bool IsSelected(EntityID id)const{ RICE_BENCHMARK_FUNCTION(); return selection.contains(id)||selected==id;}
    void Select(EntityID id,bool additive=false){
        RICE_BENCHMARK_FUNCTION();
        if(!additive)selection.clear();
        if(id&&scene->Find(id)){selection.insert(id);selected=id;}
        else if(!additive)selected=0;
    }
    void SelectChildren(EntityID id){
        RICE_BENCHMARK_FUNCTION();
        Select(id);
        for(auto& obj:scene->GetObjects())if(!obj->IsPendingDestroy())
            for(auto* p=obj->GetHierarchyParent();p;p=p->GetHierarchyParent())if(p->GetID()==id){selection.insert(obj->GetID());break;}
    }

    void DeleteSelection(){
        RICE_BENCHMARK_FUNCTION();
        if(playing)return;SyncSelection();auto ids=selection;
        Edit([&](Scene& s){ RICE_BENCHMARK_FUNCTION(); for(auto id:ids)s.Destroy(id);});
        selection.clear();selected=0;
    }

    void Record(const std::string& before){
        RICE_BENCHMARK_FUNCTION();if(playing)return;
        auto after=SceneCodec(registry).Encode(*scene);if(before==after)return;
        HistoryEntry entry;entry.beforeText=before;entry.afterText=std::move(after);
        undo.push_back(std::move(entry));if(undo.size()>100)undo.erase(undo.begin());redo.clear();++historyGeneration;
    }
    void EditScriptAttachment(EntityID id,const std::string& typeName,
                              const std::function<void(ScriptComponentBase&)>& edit = {}) {
        auto snapshot = playing ? SceneCodec(registry).Decode(editSnapshot) : nullptr;
        auto& document = snapshot ? *snapshot : *scene;
        auto* object = document.Find(id);
        auto* type = registry.Find(typeName);
        auto* script = object && type ? dynamic_cast<ScriptComponentBase*>(type->get(*object)) : nullptr;
        if (!script) throw std::runtime_error("The script component does not exist in the edited scene.");
        if (!edit) return;
        if (!playing) {
            auto before = CaptureObject(id);
            edit(*script);
            Record(before);
            return;
        }
        EditSnapshot before, after;
        before.objects[id] = CaptureState(*object);
        edit(*script);
        after.objects[id] = CaptureState(*object);
        auto updated = SceneCodec(registry).Encode(document);
        Commit(before,after);
        editSnapshot.swap(updated);
    }
    template<class F>void Edit(F fn){RICE_BENCHMARK_FUNCTION();if(playing){fn(*scene);scene->Flush();return;}auto before=CaptureAll();fn(*scene);scene->Flush();Record(before);}
    void Undo(){RICE_BENCHMARK_FUNCTION();if(playing||undo.empty())return;Apply(undo.back(),false);redo.push_back(std::move(undo.back()));undo.pop_back();}
    void Redo(){RICE_BENCHMARK_FUNCTION();if(playing||redo.empty())return;Apply(redo.back(),true);undo.push_back(std::move(redo.back()));redo.pop_back();}
    void Play(){ RICE_BENCHMARK_FUNCTION(); if(playing)return;editSnapshot=SceneCodec(registry).Encode(*scene);auto copy=SceneCodec(registry).Decode(editSnapshot);scene=std::move(copy);playing=true;paused=false;++historyGeneration;}
    void Stop(){ RICE_BENCHMARK_FUNCTION(); if(!playing)return;auto restored=SceneCodec(registry).Decode(editSnapshot);scene=std::move(restored);playing=false;paused=false;++historyGeneration;}
    void Replace(Scope<Scene> next){ RICE_BENCHMARK_FUNCTION(); scene=std::move(next);++documentRevision;++historyGeneration;playing=false;paused=false;selected=0;selection.clear();undo.clear();redo.clear();editSnapshot.clear();}
    void Load(const fs::path& path){ RICE_BENCHMARK_FUNCTION(); Replace(SceneCodec(registry).Load(path));}
};
}
