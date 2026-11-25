#include "Project/Workspace.h"
#include "Scripting/ScriptAuthoring.h"
#include "Scripting/ScriptRuntime.h"
#include <iostream>

using namespace Rice;
namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Source(int step) {
    using namespace Scripting;
    static const auto base = [] {
        auto document=ScriptDocument::New();
        ScriptId start=0,update=0;
        for (const auto& node:document.Nodes()) {
            if (node.kind==NodeKind::Start) start=node.id;
            if (node.kind==NodeKind::Update) update=node.id;
        }
        auto value = [&](ScriptId parent,NodeKind kind,std::string name,std::string expression) {
            auto id=document.Add(parent,kind,std::move(name));
            auto node=*document.Get(id);
            node.value=std::move(expression);
            Check(document.Edit(id,node),"Could not edit a script row");
            return id;
        };
        document.Add(0,NodeKind::Variable,"score");
        auto inventory=document.Add(0,NodeKind::Variable,"inventory");
        auto field=*document.Get(inventory);
        field.type="std::unordered_map<int, int>";field.value="{{2, 7}}";
        field.riceCon=true;field.riceLife="stored";
        Check(document.Edit(inventory,field),"Could not set routed dictionary");
        auto shared=document.Add(0,NodeKind::Variable,"sharedCounter");
        field=*document.Get(shared);
        field.riceCon=true;field.riceScope="global";field.riceLife="stored";
        Check(document.Edit(shared,field),"Could not set global scope");
        value(start,NodeKind::Wait,{},"0");
        auto change=value(update,NodeKind::Change,"score","score + 1");
        value(update,NodeKind::Change,"sharedCounter","sharedCounter + 1");
        value(update,NodeKind::Change,"inventory","std::unordered_map<int, int>{{2, score}}");
        auto branch=value(update,NodeKind::Condition,{},"score == (1 + 1)");
        document.Add(branch,NodeKind::Activate,"lamp");
        auto invalid=value(update,NodeKind::Condition,{},"score == 40");
        value(invalid,NodeKind::Wait,{},"score - 41");
        return std::pair{document,change};
    }();
    auto document=base.first;
    auto increment=*document.Get(base.second);
    increment.value="score + "+std::to_string(step);
    Check(document.Edit(increment.id,increment),"Could not change increment");
    return document.Source();
}
template<class T=int> T Read(Workspace& workspace, EntityID id, const char* name="score") {
    auto* execution=workspace.editor.scene->Find(id)->GetComponent<ObjectScript>()->Execution();
    Scripting::Value score;
    Check(execution && execution->Read(name,score),"Native module could not read its instance field");
    return Scripting::NumberValue<T>(score);
}
void Tick(Workspace& workspace) { workspace.Update(.05f,true); }
}

int main(int argc,char** argv) try {
    if(argc!=2) throw std::runtime_error("Test directory required");
    const auto root=fs::absolute(argv[1])/NewAssetID();
    _wputenv_s(L"RICE_SCRIPT_DATA_ROOT",(root/"Domain").c_str());
    Workspace workspace(root,true);
    auto& parent=workspace.editor.scene->CreateGameObject("Switch");
    auto& child=workspace.editor.scene->CreateGameObject("Lamp");
    child.SetParent(&parent);
    child.SetActive(false);
    const auto owner=parent.GetID(), lamp=child.GetID();
    auto& component=parent.AddComponent<ObjectScript>();
    const auto source=fs::path(u8"Scripts/한글 램프.cpp");
    auto bytes=source.generic_u8string();
    component.scriptFile={bytes.begin(),bytes.end()};
    component.BindChild("lamp",lamp);
    workspace.SaveScript(source,Source(1));
    Check(Scripting::SourceMatchesPublication(root,source,Source(1)),"Reopened document lost its saved preset source");
    workspace.Save();
    workspace.SavePrefab(owner,"Switch and lamp");
    auto& duplicate=workspace.InstantiatePrefab("Switch and lamp",{});
    auto bound=duplicate.GetComponent<ObjectScript>()->BoundChild("lamp");
    Check(bound && bound!=lamp && workspace.editor.scene->Find(bound)->GetParent()==&duplicate,"Prefab binding did not follow its own child");
    workspace.Play();
    Tick(workspace);
    Check(Read(workspace,owner)==0,"Start Wait(0) did not postpone Update");
    Tick(workspace);
    Check(Read(workspace,owner)==1&&!workspace.editor.scene->Find(lamp)->IsActive(),"First Update should leave lamp off");
    Tick(workspace);
    Check(Read(workspace,owner)==2&&workspace.editor.scene->Find(lamp)->IsActive(),"Native DLL did not activate the bound child");
    Check(Read(workspace,owner,"sharedCounter")==2,"Copied prefab shared routed fields with its original");
    Check(workspace.editor.scene->Find(bound)->IsActive(),"Prefab script did not activate its independent child");

    const auto path=root/source;
    auto edited=Source(10);
    AtomicWrite(path,edited);
    Tick(workspace);
    Check(Read(workspace,owner)==3,"External edit changed the running game");
    Check(!Scripting::SourceMatchesPublication(root,source,edited),"Unsaved external edit was accepted for a preset");
    workspace.SaveScript(source,edited);
    Tick(workspace);
    Check(Read(workspace,owner)==4,"Saving changed the running module");
    bool rejected=false;
    try { workspace.SaveScript(source,edited+"\n#error invalid_test_source\n"); }
    catch(const std::exception& e){rejected=std::string(e.what()).find("invalid_test_source")!=std::string::npos;}
    Check(rejected&&ReadFile(path)==edited,"Failed compilation replaced the saved source or lost its diagnostic");
    workspace.Stop();
    workspace.Play();
    using Dictionary=std::unordered_map<int,int>;
    Check(Read<Dictionary>(workspace,owner,"inventory").at(2)==4,"Routed dictionary was lost between Play sessions");
    Tick(workspace);Tick(workspace);
    Check(Read(workspace,owner)==10,"Next Play did not pin the newest successful module");
    workspace.Stop();

    auto package=root.parent_path()/(root.filename().string()+"-Player");
    Scripting::ExportScripts(root,package);
    fs::copy(root/"Assets",package/"Assets",fs::copy_options::recursive);
    fs::copy_file(workspace.project->FilePath(),package/(package.filename().wstring()+L".rice"));
    Workspace player(package,true);
    AtomicWrite(Scripting::ScriptModuleDirectory(package)/"build.id",NewAssetID());
    player.Play();
    Check(Read(player,owner,"sharedCounter")==5,"Global field was lost across builds in one storage domain");
    player.Stop();
    player.Play(false);Tick(player);Tick(player);
    Check(Read(player,owner)==10,"Relocated player did not execute its packaged module");
    Check(Read(player,owner,"sharedCounter")==1,"Player and editor shared an operating storage domain");
    Tick(player);Tick(player);Tick(player);
    Scripting::ScriptDocument compiled;
    compiled.SetSource(edited);
    bool located=false;
    for(const auto& row:compiled.Nodes())if(row.kind==Scripting::NodeKind::Wait && row.value=="score - 41") {
        auto& errors=player.runtimeScriptErrors[Scripting::ScriptKey(package,source)];
        auto found=errors.find(row.id);
        located=found!=errors.end() && found->second.find("finite, non-negative")!=std::string::npos;
    }
    Check(located,"Dynamic Wait error did not reach its originating editor row");
    std::cout<<"Native Save, Play, prefab binding, rollback and packaged module passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
