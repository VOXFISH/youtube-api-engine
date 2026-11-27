#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include "ScriptHost.h"
#include "NativeScriptCompiler.h"
#include "ScriptRuntime.h"
#include "ScriptAuthoring.h"
#include "Rice/Time.h"
#include "Project/SceneCodec.h"
#include "Engine/Data/DataContext.h"

namespace Rice::Scripting {
namespace {
struct LoadedModule {
    HMODULE library;
    const ModuleApi* api = nullptr;
    fs::path snapshot;
    std::optional<std::map<std::string,ScriptId>> symbols;
    bool Matches(const char* name, ScriptId id) {
        if (!id) return true;
        if (!symbols) {
            std::map<std::string,ScriptId> fields;
            if (fs::is_regular_file(snapshot)) {
                ScriptDocument compiled;
                compiled.SetSource(ReadFile(snapshot));
                for (const auto& node : compiled.Nodes())
                    if (!node.parent && node.kind==NodeKind::Variable) fields.emplace(node.name,node.id);
            }
            symbols.emplace(std::move(fields));
        }
        auto found=symbols->find(name);
        return found!=symbols->end() && found->second==id;
    }
    ~LoadedModule() { if (library) FreeLibrary(library); }
    explicit LoadedModule(const fs::path& path) : library(LoadLibraryExW(fs::absolute(path).c_str(), nullptr,
                                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)),
                                                  snapshot(fs::path(path).replace_extension(".cpp")) {
        if (library) {
            auto entry = reinterpret_cast<const ModuleApi*(*)()>(GetProcAddress(library,"RiceGetScriptApi"));
            if (entry) api = entry();
        }
        if (!api || api->abi<3 || api->abi>ModuleAbi || !api->create || !api->destroy || !api->tick || !api->read) {
            if (library) FreeLibrary(library);
            throw std::runtime_error("Could not load the saved script. Save it again with this editor.");
        }
    }
};

fs::path SharedDataDirectory() {
    std::wstring path(GetEnvironmentVariableW(L"RICE_SCRIPT_DATA_ROOT",nullptr,0), L'\0');
    if (!path.empty()) {
        auto length=GetEnvironmentVariableW(L"RICE_SCRIPT_DATA_ROOT",path.data(),static_cast<DWORD>(path.size()));
        if (length && length < path.size()) return path.substr(0,length);
    }
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)))
        throw std::runtime_error("Cannot locate the script data directory.");
    fs::path root(folder);
    CoTaskMemFree(folder);
    return root / "Rice" / "ScriptData";
}
struct PlayData {
    std::string build, session, domain;
    Data::MemoryStore game;
    Data::SqliteStore durable;
    std::unique_ptr<Data::SqliteStore> shared;
    Data::GlobalFallback global;
    bool triedShared = false;
    PlayData(const fs::path& directory, std::string buildId, std::string domainId) :
        build(std::move(buildId)), session(NewAssetID()), domain(std::move(domainId)),
        durable(directory / "scripts.sqlite3"), global(durable,build) {
        game.BeginGame(domain,build,session);
    }
    void ConnectGlobal() {
        if (triedShared) return;
        triedShared = true;
        auto directory = SharedDataDirectory() / domain;
        fs::create_directories(directory);
        shared = std::make_unique<Data::SqliteStore>(directory / "global.sqlite3");
        global.Connect(*shared,session);
    }
};
struct Session {
    fs::path project;
    bool editorPreview;
    ComponentRegistry& registry;
    PhysicsSystem& physics;
    std::function<void(int,const std::string&)> log;
    std::function<void(const std::string&,std::uint64_t,const std::string&)> runtimeDiagnostic;
    ModuleCatalog published;
    std::map<std::string,std::shared_ptr<LoadedModule>> modules;
    std::unique_ptr<PlayData> data;
    std::map<std::pair<SignalKind,std::string>,std::uint64_t> signals;

    PlayData& DataStore() {
        if (!data) {
            auto id = ScriptModuleDirectory(project) / "build.id";
            auto build = ReadFile(id);
            if (build.empty() || build.size()>128 || build.find_first_not_of("0123456789abcdefABCDEF-") != std::string::npos)
                throw std::runtime_error("Invalid script build identity.");
            auto directory = editorPreview ? project / "Settings" / "RiceMonkey" / "Data" : SharedDataDirectory() / "player" / build;
            if(editorPreview&&!fs::exists(directory)&&fs::is_directory(project/"ScriptData"))directory=project/"ScriptData";
            fs::create_directories(directory);
            data = std::make_unique<PlayData>(directory, std::move(build), editorPreview ? "editor" : "player");
        }
        return *data;
    }
};
Value Pack(glm::vec2 v) { return ToValue(Vec2{v.x,v.y}); }
Value Pack(glm::vec3 v) { return ToValue(Vec3{v.x,v.y,v.z}); }
Value Pack(Data::EncodedValue v) {
    if(v.codecVersion!=1)throw std::runtime_error("Unsupported script field codec version.");
    return BorrowEncoded(std::move(v.type),std::move(v.bytes));
}
template<class T> Value Pack(T v) { return ToValue(v); }
template<class T> T Unpack(Value value) {
    if constexpr (std::is_same_v<T,Data::EncodedValue>) {
        if(value.kind!=ValueKind::Encoded||!value.type||(!value.bytes&&value.length)||value.length>Wire::MaxBytes)
            throw std::runtime_error("Invalid encoded script field.");
        Data::EncodedValue result{value.type,1,{}};
        if(value.length)result.bytes.assign(value.bytes,value.bytes+value.length);
        return result;
    }
    else if constexpr (std::is_same_v<T,glm::vec2>) { auto v=NumberValue<Vec2>(value);return {v.x,v.y}; }
    else if constexpr (std::is_same_v<T,glm::vec3>) { auto v=NumberValue<Vec3>(value);return {v.x,v.y,v.z}; }
    else return NumberValue<T>(value);
}

class NativeExecution final : public ScriptExecution {
    std::shared_ptr<Session> session;
    std::shared_ptr<LoadedModule> module;
    ScriptComponentBase& owner;
    EntityID observedOwner{};
    void* instance{};
    std::unique_ptr<Data::DataContext> data;
    struct BoundField { std::function<Value()> get; std::function<void(Value)> set; };
    std::map<std::string,BoundField> fields;
    struct ComponentObservation {
        std::string snapshot;
        std::uint64_t version{},frame{};
    };
    std::map<std::string,ComponentObservation> observedComponents;
    std::uint64_t tick{};

    std::string ComponentSnapshot(std::string_view key) const {
        auto& object=owner.GetGameObject();
        std::ostringstream out;
        out.precision(std::numeric_limits<float>::max_digits10);
        if(key.empty()||key=="Transform"){
            auto& transform=object.transform;
            for(auto value:{transform.position,transform.rotation,transform.localScale,transform.pivot})
                out<<value.x<<' '<<value.y<<' '<<value.z<<' ';
            for(int c=0;c<4;++c)for(int r=0;r<4;++r)out<<transform.affine[c][r]<<' ';
        }
        for(const auto& type:session->registry.types){
            if(!key.empty()&&type.name!=key)continue;
            if(auto* component=type.get(object)){
                out<<std::quoted(type.name)<<' ';
                type.fields(*component,[&](const char*,Property field){
                    if(auto mesh=std::get_if<ShapeMesh*>(&field))out<<(*mesh)->revision;
                    else WriteProperty(out,field);
                    out<<' ';
                });
            }
        }
        return out.str();
    }
    std::uint64_t SignalVersion(SignalKind kind,const char* key){
        if(kind==SignalKind::CollisionStart||kind==SignalKind::CollisionEnd)
            return session->physics.ContactVersion(owner.GetGameObject().GetID(),kind==SignalKind::CollisionStart);
        if(kind==SignalKind::ComponentChange){
            auto [found,added]=observedComponents.try_emplace(key);
            auto& observation=found->second;
            if(added||observation.frame!=tick){
                auto snapshot=ComponentSnapshot(key);
                if(!added&&snapshot!=observation.snapshot)++observation.version;
                observation.snapshot=std::move(snapshot);
                observation.frame=tick;
            }
            return observation.version;
        }
        return session->signals[{kind,key}];
    }

    bool Move(double x, double y, double z, MoveUnit unit) {
        auto& object=owner.GetGameObject();
        auto& scene=object.GetScene();
        double interval;
        switch(unit) {
            case MoveUnit::Second: interval=scene.UnscaledDeltaTime(); break;
            case MoveUnit::GameSecond: interval=scene.GameDeltaTime(); break;
            case MoveUnit::Frame: interval=1; break;
            default: return false;
        }
        auto delta=glm::dvec3(x,y,z)*interval;
        auto representable=[](double v){return std::isfinite(v)&&std::abs(v)<=std::numeric_limits<float>::max();};
        if (!representable(delta.x)||!representable(delta.y)||!representable(delta.z)) return false;
        if (delta==glm::dvec3(0)) return true;
        const auto before=object.transform.position;
        try { object.TranslateWorld(Vector3(delta)); }
        catch (const std::exception&) { return false; }
        const auto& after=object.transform.position;
        if (std::isfinite(after.x)&&std::isfinite(after.y)&&std::isfinite(after.z)) return true;
        object.transform.position=before;
        return false;
    }
    GameObject* Resolve(const char* binding) const {
        auto& origin=owner.GetGameObject();
        if (!binding || !*binding) return &origin;
        auto* target = origin.GetScene().Find(owner.BoundChild(binding));
        if (!target || target->IsPoolAvailable()) return nullptr;
        for (auto* p=target->GetHierarchyParent(); p; p=p->GetHierarchyParent())
            if (p==&origin) return target;
        return nullptr;
    }
    Component* TargetComponent(GameObject& obj, const char* type) const {
        auto* info = session->registry.Find(type);
        return info ? info->get(obj) : nullptr;
    }
    int Activate(const char* binding, const char* type, bool active) {
        auto* target=Resolve(binding);
        if (!target) return 0;
        if (type && *type) {
            auto* c=TargetComponent(*target,type);
            if (!c) return 0;
            c->enabled=active;
            return !active && c==&owner ? -1 : 1;
        }
        target->SetActive(active);
        return !active && target==&owner.GetGameObject() ? -1 : 1;
    }
    bool IsActive(const char* binding, const char* type) const {
        auto* target=Resolve(binding);
        if (!target || !target->IsActive()) return false;
        if (!type || !*type) return true;
        auto* component=TargetComponent(*target,type);
        return component && component->enabled;
    }
    bool ReadChild(const char* binding, const char* name, Value& out) {
        auto* target=Resolve(binding);
        if (!target || !binding || !*binding || !name) return false;
        std::istringstream saved(owner.childReads);
        std::string line;
        while (std::getline(saved,line)) {
            std::string key,type,variable;
            ScriptId symbol=0;
            std::istringstream row(line);
            if (!(row>>std::quoted(key)>>std::quoted(type)>>std::quoted(variable))) continue;
            row>>symbol;
            if (key!=binding || variable!=name) continue;
            auto* child=dynamic_cast<ScriptComponentBase*>(TargetComponent(*target,type.c_str()));
            if (child) if(auto* execution=child->Execution()) return execution->Read(name,out,symbol);
        }
        return false;
    }
    template<class T> BoundField Bind(const FieldBinding& binding, Value initial) {
        auto value=Unpack<T>(initial);
        const auto access = std::string_view(binding.access);
        auto permission = access=="policy" ? Data::AccessPolicy::Named(binding.policy)
                        : access=="open" ? Data::AccessPolicy::Open() : Data::AccessPolicy::Locked();
        auto bind = [&]<Data::Scope S,Data::Lifetime L>() {
            return data->Bind<T,S,L>(binding.id,value,binding.name,permission);
        };
        auto field = [&] {
            if (std::string_view(binding.scope)=="global") {
                if (std::string_view(binding.life)!="stored") throw std::runtime_error("Global fields must be stored.");
                return bind.template operator()<Data::Scope::Global,Data::Lifetime::Stored>();
            }
            if (std::string_view(binding.life)=="slot") return bind.template operator()<Data::Scope::Build,Data::Lifetime::Slot>();
            if (std::string_view(binding.life)=="stored") return bind.template operator()<Data::Scope::Build,Data::Lifetime::Stored>();
            return bind.template operator()<Data::Scope::Build,Data::Lifetime::Game>();
        }();
        return {[field]() {return Pack(field.Get());},[field](Value v) mutable {field.Set(Unpack<T>(v));}};
    }
    BoundField& Field(const FieldBinding& binding, Value initial) {
        if (auto it=fields.find(binding.id);it!=fields.end()) return it->second;
        auto& storage=session->DataStore();
        if (std::string_view(binding.scope)=="global") {
            try { storage.ConnectGlobal(); }
            catch (const std::exception& error) {
                session->log(int(Severity::Warning),std::string("Using the local global-data mirror. ")+error.what());
            }
        }
        if (!data) {
            auto slot=Data::DataContext::PlayerMainSlot();
            if (slot.empty() && session->editorPreview) slot = "preview";
            data=std::make_unique<Data::DataContext>(
                Data::DataIdentity{storage.domain,storage.build,storage.session,owner.dataObjectKey},
                storage.game,storage.durable,&storage.global,slot.empty()?nullptr:std::make_shared<Data::SlotContext>(slot));
        }
        BoundField field;
        switch(initial.kind) {
            case ValueKind::Int: field=Bind<int>(binding,initial);break;
            case ValueKind::Float: field=Bind<float>(binding,initial);break;
            case ValueKind::Bool: field=Bind<bool>(binding,initial);break;
            case ValueKind::Vector2: field=Bind<glm::vec2>(binding,initial);break;
            case ValueKind::Vector3: field=Bind<glm::vec3>(binding,initial);break;
            case ValueKind::Encoded: field=Bind<Data::EncodedValue>(binding,initial);break;
            default: throw std::runtime_error("Unsupported routed script field type.");
        }
        return fields.emplace(binding.id,std::move(field)).first->second;
    }
    bool Route(const FieldBinding* field,Value& value,bool write) {
        try {
            if (!field) return false;
            auto& bound=Field(*field,value);
            if (write) bound.set(value); else value=bound.get();
            return true;
        } catch(const std::exception& error) {
            session->log(int(Severity::Error),error.what());return false;
        }
    }
public:
    NativeExecution(std::shared_ptr<Session> play, std::shared_ptr<LoadedModule> binary, ScriptComponentBase& script)
        :session(std::move(play)),module(std::move(binary)),owner(script) {
        Host host{};
        host.user=this;
        host.timerDelta=[](void* p,bool gameSeconds){
            auto& scene=static_cast<NativeExecution*>(p)->owner.GetGameObject().GetScene();
            return gameSeconds?scene.GameDeltaTime():scene.UnscaledDeltaTime();
        };
        host.signalVersion=[](void* p,SignalKind kind,const char* key){return static_cast<NativeExecution*>(p)->SignalVersion(kind,key);};
        host.publishSignal=[](void* p,SignalKind kind,const char* key){++static_cast<NativeExecution*>(p)->session->signals[{kind,key}];};
        host.log=[](void* p,int severity,const char* msg){static_cast<NativeExecution*>(p)->session->log(severity,msg?msg:"");};
        host.diagnostic=[](void* p,std::uint64_t row,const char* message){
            auto& self=*static_cast<NativeExecution*>(p);
            if(self.session->runtimeDiagnostic)
                self.session->runtimeDiagnostic(self.owner.scriptFile,row,message?message:"Script runtime error.");
        };
        host.move=[](void* p,double x,double y,double z,MoveUnit unit){return static_cast<NativeExecution*>(p)->Move(x,y,z,unit);};
        host.gameSpeed=[](void*)->float{return Time::timeScale;};
        host.setGameSpeed=[](void*,float speed){Time::timeScale=speed;};
        host.activate=[](void* p,const char* key,const char* type,bool v){return static_cast<NativeExecution*>(p)->Activate(key,type,v);};
        host.isActive=[](void* p,const char* key,const char* type){return static_cast<NativeExecution*>(p)->IsActive(key,type);};
        host.readChild=[](void* p,const char* key,const char* name,Value* out){return out&&static_cast<NativeExecution*>(p)->ReadChild(key,name,*out);};
        host.contact=[](void* p,int phase,int index,Contact* out){
            auto& self=*static_cast<NativeExecution*>(p);
            return out&&self.session->physics.ContactAt(self.owner.GetGameObject().GetID(),phase,index,*out);
        };
        host.readField=[](void* p,const FieldBinding* f,Value* v){return v&&static_cast<NativeExecution*>(p)->Route(f,*v,false);};
        host.writeField=[](void* p,const FieldBinding* f,const Value* v){
            if(!v)return false;auto copy=*v;return static_cast<NativeExecution*>(p)->Route(f,copy,true);
        };
        host.bindField=[](void* p,const FieldBinding* field,const Value* initial){
            auto& self=*static_cast<NativeExecution*>(p);
            if(!field||!initial)return false;
            try { self.Field(*field,*initial);return true; }
            catch(const std::exception& error) { self.session->log(int(Severity::Error),error.what());return false; }
        };
        instance=module->api->create(&host);
        if (!instance) throw std::runtime_error("Unable to create the native script instance.");
        observedOwner=owner.GetGameObject().GetID();
        session->physics.TrackScriptContacts(observedOwner);
    }
    ~NativeExecution() override {
        if(instance) module->api->destroy(instance);
        session->physics.UntrackScriptContacts(observedOwner);
    }
    void Tick(float dt) override { ++tick;module->api->tick(instance,dt); }
    bool TriggerBlocking(uint64_t row) const override {
        return module->api->abi>=5 && module->api->triggerBlocking && module->api->triggerBlocking(instance,row);
    }
    bool Read(const char* name, Value& value, uint64_t symbol) override {
        return name && module->Matches(name,symbol) && module->api->read(instance,name,&value);
    }
};
}
void AttachNativeScripts(Scene& scene,ComponentRegistry& registry,PhysicsSystem& physics,const fs::path& project,bool editorPreview,
                         std::function<void(int,const std::string&)> log,
                         std::function<void(const std::string&,std::uint64_t,const std::string&)> runtimeDiagnostic) {
    auto state=std::make_shared<Session>(Session{project,editorPreview,registry,physics,std::move(log),
        std::move(runtimeDiagnostic),PublishedScripts(project),{},{}});
    scene.createScriptExecution=[state](ScriptComponentBase& script)->std::unique_ptr<ScriptExecution> {
        try {
            auto key=ScriptKey(state->project,fs::path(std::u8string(script.scriptFile.begin(),script.scriptFile.end())));
            auto it=state->published.find(key);
            if(it==state->published.end()) {
                state->log(int(Severity::Warning),"No saved native build for script: "+key);
                return {};
            }
            auto& module=state->modules[key];
            if(!module)module=std::make_shared<LoadedModule>(it->second.binary);
            return std::make_unique<NativeExecution>(state,module,script);
        } catch(const std::exception& error) {
            state->log(int(Severity::Error),error.what());
            return {};
        }
    };
}
}
