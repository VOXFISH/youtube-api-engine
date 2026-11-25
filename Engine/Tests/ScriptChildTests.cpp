#include "Project/Workspace.h"
#include "Scripting/ScriptRuntime.h"
#include <iostream>

using namespace Rice;
namespace {
void Require(bool value,const char* message) {
    if (!value) throw std::runtime_error(message);
}
constexpr const char* ChildSource = R"CPP(
#include "Scripting/ScriptRuntime.h"
using namespace Rice::Scripting;
struct GameScript : ScriptBase {
    int value = Active() ? 11 : 29;
    /*rice:reader*/ bool Read(const char* key, Value& result) const {
        if(std::string_view(key)!="value") return false;
        result=ToValue(value); return true;
    }
};
)CPP";
constexpr const char* ParentSource = R"CPP(
#include "Scripting/ScriptRuntime.h"
using namespace Rice::Scripting;
struct GameScript : ScriptBase {
    int frame = 0;
    int captured = 0;
    int following = 0;
    Task<> Update() {
        ++frame;
        if(frame==1) {
            captured=ReadChild<int>("reading","value");
        }
        if(frame==2) {
            Activate("lamp","",true);
            ++following;
        }
        if(frame==3) {
            if(captured==29) {
                Activate("","ObjectScript",false);
                ++following;
            } else {
                captured=ReadChild<int>("reading","value");
            }
        }
        co_return;
    }
    /*rice:reader*/ bool Read(const char* key,Value& result) const {
        const std::string_view name(key);
        if(name=="captured") result=ToValue(captured);
        else if(name=="following") result=ToValue(following);
        else if(name=="frame") result=ToValue(frame);
        else return false;
        return true;
    }
};
RICE_SCRIPT_MODULE(GameScript)
)CPP";
struct Family { EntityID parent,grandchild,lamp; };
Family MakeFamily(Scene& scene,bool active) {
    auto& parent=scene.CreateGameObject(active?"Right parent":"Left parent");
    auto& intermediate=scene.CreateGameObject("Intermediate");
    intermediate.SetParent(&parent);
    intermediate.SetActive(active);
    auto& child=scene.CreateGameObject("Reading");
    child.SetParent(&intermediate);
    child.AddComponent<ObjectScript>().scriptFile="Scripts/Child.cpp";
    auto& lamp=scene.CreateGameObject("Lamp");
    lamp.SetParent(&parent);lamp.SetActive(false);
    auto& component=parent.AddComponent<ObjectScript>();
    component.scriptFile="Scripts/Parent.cpp";
    component.BindChild("reading",child.GetID());
    component.BindChild("lamp",lamp.GetID());
    component.childReads="\"reading\" \"ObjectScript\" \"value\"\n";
    return {parent.GetID(),child.GetID(),lamp.GetID()};
}
int Read(Scene& scene,EntityID id,const char* key) {
    auto* execution=scene.Find(id)->GetComponent<ObjectScript>()->Execution();
    Scripting::Value value;
    Require(execution&&execution->Read(key,value),"Cannot read parent script field.");
    return Scripting::NumberValue<int>(value);
}
bool Logged(const Workspace& workspace,std::string_view text) {
    return std::any_of(workspace.logs.begin(),workspace.logs.end(),[&](const auto& entry){
        return entry.find(text)!=std::string::npos;
    });
}
}
int main(int argc,char** argv) try {
    Require(argc==2,"Expected a test directory.");
    auto root=fs::absolute(argv[1])/NewAssetID();
    Workspace workspace(root,true);
    const auto trace=root/"free-hooks.txt";
    auto bytes=trace.generic_u8string();
    std::ostringstream path;
    path<<"std::filesystem::path(u8"<<std::quoted(std::string(bytes.begin(),bytes.end()))<<")";
    auto freeHooks="#include <filesystem>\n#include <fstream>\n"
        "void Start() { std::ofstream("+path.str()+") << 'S'; }\n"
        "void Update(float dt) { if (dt>0) std::ofstream("+path.str()+",std::ios::app) << 'U'; }\n";
    workspace.SaveScript("Scripts/Free.cpp",freeHooks);
    Require(!fs::exists(trace)&&ReadFile(root/"Scripts/Free.cpp")==freeHooks,
            "Saving unannotated C++ must preserve its source and must not call its hooks.");
    workspace.editor.scene->CreateGameObject("Free hooks").AddComponent<BlankScript>().scriptFile="Scripts/Free.cpp";
    workspace.SaveScript("Scripts/Child.cpp",ChildSource);
    workspace.SaveScript("Scripts/Parent.cpp",ParentSource);
    auto left=MakeFamily(*workspace.editor.scene,false);
    auto right=MakeFamily(*workspace.editor.scene,true);
    workspace.Save();
    workspace.Play();
    auto& scene=*workspace.editor.scene;
    auto tick=[&]{workspace.Update(.05f,true);};
    tick();
    Require(ReadFile(trace)=="SU","The native adapter did not invoke both free lifecycle functions.");
    Require(!scene.Find(left.grandchild)->IsActive(),"Grandchild must be inactive.");
    Require(Read(scene,left.parent,"captured")==29&&Read(scene,right.parent,"captured")==11,
            "Each parent must read its own bound grandchild, including inactive descendants.");
    scene.Destroy(left.lamp);
    scene.FlushDestroyed();
    tick();
    Require(Logged(workspace,"Activation target is unavailable: lamp"),"Missing activation target must log an error.");
    Require(Read(scene,left.parent,"following")==1,"Execution must reach the row after the missing target.");
    Require(scene.Find(right.lamp)->IsActive(),"Other component's target should still activate.");
    scene.Find(right.parent)->GetComponent<ObjectScript>()->childReads.clear();
    tick();
    Require(!scene.Find(left.parent)->GetComponent<ObjectScript>()->enabled&&Read(scene,left.parent,"following")==1,
            "Self-deactivation must immediately abandon its invocation.");
    Require(Logged(workspace,"Child variable is unavailable: value"),"Removed saved grant must deny child reads.");
    Require(Read(scene,right.parent,"captured")==11,"A denied child read must not overwrite the existing value.");
    tick();
    Require(Read(scene,left.parent,"frame")==3,"A disabled script must not advance.");
    workspace.Stop();
    std::cout<<"Saved child access, inactive descendants, per-component binding and runtime failure semantics passed\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
