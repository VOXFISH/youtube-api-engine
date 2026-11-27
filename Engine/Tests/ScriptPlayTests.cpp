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
        const auto addAmount=document.Add(0,NodeKind::Function,"AddAmount");
        auto function=*document.Get(addAmount);
        function.type="int";function.args="int current, int increment";
        Check(document.Edit(addAmount,function),"Could not declare a two-parameter script function");
        value(addAmount,NodeKind::EndFunction,{},"current + increment");
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
    increment.value="AddAmount(score, AddAmount(0, "+std::to_string(step)+"))";
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

void CheckCrossScriptSignals(const fs::path& root) {
    using namespace Scripting;
    Workspace workspace(root,true);
    auto source=ScriptDocument::New(), listener=ScriptDocument::New();
    auto hook=[](const ScriptDocument& doc,NodeKind kind){
        for(const auto& node:doc.Nodes())if(node.kind==kind)return node.id;
        throw std::runtime_error("Missing lifecycle hook");
    };
    const auto event=source.Add(0,NodeKind::Event,"pulse");
    const auto function=source.Add(0,NodeKind::Function,"SendPulse");
    for(auto id:{event,function}){
        auto row=*source.Get(id);
        row.isPublic=true;
        Check(source.Edit(id,row),"Could not publish the signal declaration");
    }
    auto emit=*source.Get(source.Add(function,NodeKind::EmitEvent));
    emit.value=SignalKey(*source.Get(event));
    Check(source.Edit(emit.id,emit),"Could not set the event source");
    source.Add(hook(source,NodeKind::Update),NodeKind::Call,"SendPulse");
    const fs::path senderPath="Scripts/SignalSource.cpp";
    const auto senderKey=ScriptKey(root,senderPath);
    workspace.SaveScript(senderPath,source.Source());

    const auto start=hook(listener,NodeKind::Start);
    const auto update=hook(listener,NodeKind::Update);
    auto observe=[&](ScriptId parent,const std::string& type,const std::string& value,const char* counter,const char* repeat,bool block){
        listener.Add(0,NodeKind::Variable,counter);
        auto id=listener.Add(parent,NodeKind::Trigger);
        auto row=*listener.Get(id);
        row.type=type;row.value=value;row.args=repeat;row.blockOthers=block;
        Check(listener.Edit(id,row),"Could not choose a trigger source");
        auto add=*listener.Get(listener.Add(id,NodeKind::Change,counter));
        add.value=std::string(counter)+" + 1";
        Check(listener.Edit(add.id,add),"Could not edit the signal handler");
        return id;
    };
    auto speed=*listener.Get(listener.Add(start,NodeKind::Change,"Game Speed"));
    speed.value="0.5f";listener.Edit(speed.id,speed);
    auto interval=*listener.Get(listener.Add(0,NodeKind::Variable,"period"));
    interval.type="float";interval.value="0.125f";listener.Edit(interval.id,interval);
    listener.Add(0,NodeKind::Variable,"samples");
    auto sample=*listener.Get(listener.Add(0,NodeKind::Function,"SamplePeriod"));
    sample.type="float";listener.Edit(sample.id,sample);
    auto countSample=*listener.Get(listener.Add(sample.id,NodeKind::Change,"samples"));
    countSample.value="samples + 1";listener.Edit(countSample.id,countSample);
    auto result=*listener.Get(listener.Add(sample.id,NodeKind::EndFunction));
    result.value="period";listener.Edit(result.id,result);
    const auto eventKey=SignalKey(*source.Get(event),senderKey);
    const auto once=observe(start,"Event",eventKey,"singles","once",false);
    observe(start,"Event",eventKey,"repeats","every",false);
    auto stopped=observe(start,"Event",eventKey,"stopped","every",false);
    Check(listener.Add(stopped,NodeKind::BreakTrigger)!=0,"Could not add a trigger break");
    observe(start,"Game Seconds","period","gameTicks","every",false);
    observe(start,"Seconds","period","realTicks","every",false);
    observe(start,"Function",SignalKey(*source.Get(function),senderKey),"functionSeen","once",false);
    observe(start,"Seconds","period","unlocked","once",true);
    observe(update,"Seconds","SamplePeriod()","sampled","once",false);
    observe(update,"Event",eventKey,"blocks","once",true);
    listener.Add(0,NodeKind::Variable,"updates");
    auto after=*listener.Get(listener.Add(update,NodeKind::Change,"updates"));
    after.value="updates + 1";listener.Edit(after.id,after);
    workspace.SaveScript("Scripts/Listener.cpp",listener.Source());
    auto invalid=listener;
    auto wrongKind=*invalid.Get(once);
    wrongKind.value=SignalKey(*source.Get(function),senderKey);
    Check(invalid.Edit(once,wrongKind),"Could not set up the stale external reference");
    bool rejected=false;
    try { workspace.SaveScript("Scripts/Listener.cpp",invalid.Source()); }
    catch(const std::runtime_error& error){ rejected=std::string_view(error.what()).find("Selected public signal")!=std::string_view::npos; }
    Check(rejected&&ReadFile(root/"Scripts/Listener.cpp")==listener.Source(),"An invalid external event replaced the saved script");
    auto& observer=workspace.editor.scene->CreateGameObject("Listener");
    const auto id=observer.GetID();
    observer.AddComponent<ObjectScript>().scriptFile="Scripts/Listener.cpp";
    auto& sender=workspace.editor.scene->CreateGameObject("Sender");
    sender.AddComponent<ObjectScript>().scriptFile="Scripts/SignalSource.cpp";
    workspace.Play();
    for(int i=0;i<12;++i)Tick(workspace);
    Check(Read(workspace,id,"singles")==1&&Read(workspace,id,"stopped")==1&&Read(workspace,id,"repeats")>2,
          "Public event handlers must distinguish once, every and break across script modules");
    Check(Read(workspace,id,"functionSeen")==1&&Read(workspace,id,"blocks")==1&&Read(workspace,id,"updates")>1,
          "A function signal must fire and the blocking triggers must release subsequent updates");
    Check(Read(workspace,id,"unlocked")==1&&Read(workspace,id,"realTicks")==4&&Read(workspace,id,"gameTicks")==2,
          "Single fires once and repeating time triggers use the selected clock at half game speed");
    Check(Read(workspace,id,"sampled")==1&&Read(workspace,id,"samples")==1,
          "A time trigger in Every Frame evaluates its duration function only on registration");
    workspace.Stop();
}

void CheckWorldSignals(const fs::path& root) {
    using namespace Scripting;
    Workspace workspace(root,true);
    auto doc=ScriptDocument::New();
    ScriptId start{};
    for(const auto& node:doc.Nodes())if(node.kind==NodeKind::Start)start=node.id;
    auto subscribe=[&](const char* signal,const char* key,const char* counter){
        doc.Add(0,NodeKind::Variable,counter);
        auto trigger=*doc.Get(doc.Add(start,NodeKind::Trigger));
        trigger.type=signal;trigger.value=key;trigger.args="every";
        Check(doc.Edit(trigger.id,trigger),"Could not select the world signal");
        auto change=*doc.Get(doc.Add(trigger.id,NodeKind::Change,counter));
        change.value=std::string(counter)+" + 1";doc.Edit(change.id,change);
    };
    subscribe("Collision Start","","entered");
    subscribe("Collision End","","exited");
    subscribe("Component Change","Camera","cameras");
    subscribe("Component Change","Transform","moves");
    doc.Add(0,NodeKind::Variable,"oldWaits");
    auto legacy=doc.Source();
    constexpr std::string_view updateHeader="Task<> Update() {";
    auto where=legacy.find(updateHeader);
    Check(where!=std::string::npos,"Expected the native Every Frame hook");
    // A Timer saved by the previous editor waits on each visit and may suspend inside its body.
    legacy.insert(where+updateHeader.size(),"\n        if (co_await TimerSeconds(0, true, 91ULL)) { co_await Wait(0); ++oldWaits; }\n");
    workspace.SaveScript("Scripts/WorldSignals.cpp",legacy);
    auto& object=workspace.editor.scene->CreateGameObject("Observer");
    object.transform.position={30,30,30};
    object.AddComponent<BoxCollider>();
    object.AddComponent<ObjectScript>().scriptFile="Scripts/WorldSignals.cpp";
    auto& peer=workspace.editor.scene->CreateGameObject("Peer");
    peer.transform.position={35,30,30};peer.AddComponent<BoxCollider>();
    const auto id=object.GetID(),other=peer.GetID();
    workspace.Play();Tick(workspace);
    auto& active=*workspace.editor.scene->Find(id);
    auto& otherObject=*workspace.editor.scene->Find(other);
    otherObject.transform.position=active.transform.position;
    auto& camera=active.AddComponent<CameraComponent>();
    Tick(workspace);Tick(workspace);
    Check(Read(workspace,id,"entered")==1&&Read(workspace,id,"cameras")==1,"Collision entry and component addition fire once");
    camera.enabled=false;
    Tick(workspace);
    camera.fieldOfView+=5;
    Tick(workspace);
    active.RemoveComponent<CameraComponent>();
    active.transform.position.x+=5;
    Tick(workspace);Tick(workspace);
    Check(Read(workspace,id,"exited")==1&&Read(workspace,id,"entered")==1,"Separation fires Collision End once");
    Check(Read(workspace,id,"cameras")==4&&Read(workspace,id,"moves")==1,
          "Component Change tracks enabled, serialized properties, removal and owner Transform");
    Check(Read(workspace,id,"oldWaits")>=2,"Reopened inline Timer must still wait and run its body on each visit");
    workspace.Stop();
}

void CheckMovement(const fs::path& root) {
    using namespace Scripting;
    auto document=ScriptDocument::New();
    ScriptId update=0;
    for(const auto& row:document.Nodes())if(row.kind==NodeKind::Update)update=row.id;
    document.Add(0,NodeKind::Variable,"frames");
    auto change=[&](const std::string& name,const std::string& expression) {
        auto id=document.Add(update,NodeKind::Change,name);
        auto row=*document.Get(id);
        row.value=expression;
        Check(document.Edit(id,row),"Could not edit a Change Variable row");
    };
    change("frames","frames + 1");
    change("Game Speed","frames == 1 ? 0.5f : (frames == 2 ? 0.0f : 4.0f)");
    auto move=[&](const std::string& rate,std::array<std::string,3> coordinates,const std::string& unit) {
        auto id=document.Add(update,NodeKind::Move);
        auto row=*document.Get(id);
        row.value=rate;
        row.moveCoordinates=coordinates;
        row.args=unit;
        Check(document.Edit(id,row),"Could not edit a Move row");
        ScriptDocument reopened;
        reopened.SetSource(document.Source());
        auto* saved=reopened.Get(id);
        Check(saved && saved->kind==NodeKind::Move && saved->value==rate &&
              saved->moveCoordinates==coordinates && saved->args==unit,"Move fields did not survive reopening");
    };
    move("2",{"3","4","0"},"second");
    move("",{"0","0","3"},"game second");
    move("-1.5",{"0","2","0"},"frame");
    move("0",{"10","10","10"},"frame");
    move("5",{"0","0","0"},"frame");
    for(const auto& issue:document.Validate())Check(issue.warning,issue.message.c_str());

    Workspace workspace(root,true);
    auto& parent=workspace.editor.scene->CreateGameObject("Rotated parent");
    parent.transform.rotation.y=90;
    parent.transform.localScale={2,3,4};
    auto& mover=workspace.editor.scene->CreateGameObject("Mover");
    mover.SetParent(&parent);
    auto id=mover.GetID();
    mover.AddComponent<ObjectScript>().scriptFile="Scripts/Movement.cpp";
    workspace.SaveScript("Scripts/Movement.cpp",document.Source());
    workspace.Play();
    auto position=[&]{return workspace.editor.scene->Find(id)->GetWorldPivot();};
    auto step=[&](float realSeconds,Vector3 displacement) {
        auto before=position();
        workspace.Update(realSeconds,true);
        Check(glm::distance(position()-before,displacement)<.0001f,"Move applied the wrong world displacement");
    };
    step(.05f,{.06f,-1.42f,.15f});    // Starts at normal speed.
    step(.05f,{.06f,-1.42f,.075f});   // Half speed from the preceding Update.
    workspace.Pause();
    step(.25f,{0,0,0});
    workspace.Resume();
    step(.05f,{.06f,-1.42f,0});       // At zero speed the real and frame units still run.
    step(.25f,{.3f,-1.1f,3.f});      // A full game second elapses at 4x.
    workspace.Stop();
    workspace.Play();
    step(.05f,{.06f,-1.42f,.15f});
    workspace.Stop();
}
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
    player.Stop();
    CheckMovement(root.parent_path()/NewAssetID());
    CheckCrossScriptSignals(root.parent_path()/NewAssetID());
    CheckWorldSignals(root.parent_path()/NewAssetID());
    std::cout<<"Native Save, Play, prefab binding, rollback and packaged module passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
