#include "Editor/ScriptPanel.h"
#include <iostream>

namespace Rice {
using namespace Scripting;

static void Check(bool result,const char* failure){
    if(!result)throw std::runtime_error(failure);
}
static ScriptId First(const ScriptDocument& doc,NodeKind kind){
    for(auto& n:doc.Nodes())if(n.kind==kind)return n.id;
    throw std::runtime_error("Missing script row");
}
static void EditValue(ScriptEditingSession& session,ScriptView& view,ScriptId id,std::string text){
    auto node=*view.document.Get(id);
    node.value=std::move(text);
    Check(view.document.Edit(id,node),"Expected an editable row");
    session.Changed(view,id);
}
struct ScriptPanelTestAccess {
    static void StrategyRoundTrip(){
        using Action=ScriptPanel::StrategyAction;
        ScriptDocument doc=ScriptDocument::New();
        auto klass=doc.Add(0,NodeKind::Class,"Walking");
        auto f=doc.Add(klass,NodeKind::Function,"Step");
        auto method=*doc.Get(f);method.args="float speed, int count";method.type="float";
        Check(doc.Edit(f,method),"Could not author a strategy method");
        auto methods=ScriptPanel::StrategyMethods(doc);
        Check(methods.size()==1&&methods[0].signature=="StrategySlot<float, float, int>",
              "Strategy compatibility must use native return and parameter types");
        for(const auto& action:std::vector<ScriptPanel::StrategyLine>{
            {Action::Replace,"movement","Walking::Step",{},false},
            {Action::Invoke,"movement","0.5f, 2","score",false},
            {Action::Invoke,"movement","speed, count",{},true}}){
            auto owner=action.inClass?f:First(doc,NodeKind::Start);
            auto id=doc.Add(owner,NodeKind::Custom);
            auto row=*doc.Get(id);row.value=ScriptPanel::StrategySource(action);
            Check(doc.Edit(id,row),"Could not write the native strategy action");
            ScriptDocument reopened;reopened.SetSource(doc.Source());
            auto parsed=ScriptPanel::ParseStrategy(*reopened.Get(id));
            Check(parsed.action==action.action&&parsed.slot==action.slot&&parsed.argument==action.argument&&
                  parsed.output==action.output&&parsed.inClass==action.inClass,
                  "Strategy parameters, output, method or ownership did not survive native source import");
        }
    }
    static void Run(const fs::path& directory){
        StrategyRoundTrip();
        Workspace workspace(directory,true);
        auto& object=workspace.editor.scene->CreateGameObject("Owner");
        auto& owner=object.AddComponent<ObjectScript>();
        ScriptPanel panel;
        panel.Open(workspace,object,"ObjectScript");
        auto& entry=panel.entries.front();
        auto& main=entry.main;
        auto var=main.document.Add(0,NodeKind::Variable,"score");
        auto func=main.document.Add(0,NodeKind::Function,"increase");
        auto change=main.document.Add(func,NodeKind::Change,"score");
        auto wait=main.document.Add(func,NodeKind::Wait);
        EditValue(entry,main,change,"score + 1");
        EditValue(entry,main,wait,"0.25f");
        auto start=First(main.document,NodeKind::Start);
        auto call=main.document.Add(start,NodeKind::Call,"increase");
        auto otherCall=main.document.Add(First(main.document,NodeKind::Update),NodeKind::Call,"increase");
        Check(main.document.Get(call)!=nullptr,"Call Function adds an executable row");
        auto local=main.document.Add(start,NodeKind::Variable,"started");
        Check(!main.document.CanSee(local,First(main.document,NodeKind::Update)),"Start locals escaped into Update");
        main.Find(func);
        Check(main.matches.size()==2&&main.selected==call,"Find Instances did not select the first function call");
        main.Navigate(-1);Check(main.selected==call,"Find wrapped before the first result");
        main.Navigate(1);main.Navigate(1);Check(main.selected==otherCall,"Find wrapped beyond the last result");
        unsigned a=entry.Separate(func),b=entry.Separate(func);
        auto& first=entry.detached.at(a);
        auto& second=entry.detached.at(b);
        first.Find(var);
        Check(first.matches.size()==1&&first.selected==change,"Detached Find crossed its scope");
        EditValue(entry,first,change,"score + 2");
        EditValue(entry,second,change,"score + 3");
        entry.Apply(second);entry.Apply(first);
        Check(main.document.Get(change)->value=="score + 3","An older detached edit replaced a newer one");

        ImGui::CreateContext();
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,800};io.DeltaTime=1.f/60;
        io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        auto frame=[&]{ImGui::NewFrame();panel.Draw(workspace,{});ImGui::Render();};
        main.open=false;frame();
        Check(ImGui::FindWindowByName(panel.WindowLabel(entry,first,a).c_str())->Active&&
              ImGui::FindWindowByName(panel.WindowLabel(entry,second,b).c_str())->Active,
              "Detached windows were closed along with the main window");
        panel.Open(workspace,object,"ObjectScript");
        main.Find(func);main.findOpen=true;
        frame();frame();
        io.AddKeyEvent(ImGuiKey_RightArrow,true);frame();
        io.AddKeyEvent(ImGuiKey_RightArrow,false);frame();
        io.AddKeyEvent(ImGuiKey_Enter,true);frame();
        io.AddKeyEvent(ImGuiKey_Enter,false);frame();
        Check(main.open&&!main.findOpen&&main.selected==otherCall,"Arrow navigation and Enter did not return focus to the selected instance");
        ImGui::DestroyContext();

        auto source=workspace.project->assets.Root()/"test-script.cpp";
        auto original=main.document.Source();
        AtomicWrite(source,original);
        panel.Load(workspace,entry,source,false);
        Check(owner.scriptFile=="test-script.cpp"&&!entry.Dirty(),"Load File did not replace the component link");
        auto scriptKey=ScriptKey(workspace.project->root,source);
        workspace.runtimeScriptErrors[scriptKey][wait]="Wait duration must be a finite, non-negative number.";
        Check(panel.HasError(workspace,object.GetID(),"ObjectScript")&&!entry.HasError(),
              "Runtime row errors must color the Inspector independently of Save validation");
        workspace.runtimeScriptErrors.clear();
        EditValue(entry,main,wait,"0.75f");
        ScriptDocument external;external.SetSource(original);
        auto modified=*external.Get(change);modified.value="score + 4";
        external.Edit(change,modified);
        entry.External(external.Source());
        Check(main.document.Get(change)->value=="score + 4"&&main.document.Get(wait)->value=="0.75f",
              "IDE round-trip lost an independent item edit");
        ScriptTargets bindings;
        bindings.children["item"]=object.GetID();bindings.reads["item"]={"ObjectScript","score"};
        panel.WriteBindings(owner,bindings);
        Check(panel.Bindings(owner)==bindings,"Scene bindings failed to round-trip");
        panel.Load(workspace,entry,source,true);
        Check(entry.targets.children.empty()&&entry.targets.reads.empty(),"Preset inherited scene targets");
        main.document.Add(0,NodeKind::Variable,"score");
        Check(!panel.Save(workspace,entry,source),"Save accepted duplicate names");
        Check(panel.HasError(workspace,object.GetID(),"ObjectScript"),"Inspector omitted failed Save");
        Check(ReadFile(source)==original,"Failed Save replaced the last source");

        // A second component using this C++ source has its own scene targets.
        auto& sibling=workspace.editor.scene->CreateGameObject("Second owner");
        auto& siblingScript=sibling.AddComponent<ObjectScript>();siblingScript.scriptFile=owner.scriptFile;
        auto& middle=workspace.editor.scene->CreateGameObject("Group");middle.SetParent(&object);
        auto& light=workspace.editor.scene->CreateGameObject("Light");light.SetParent(&middle);light.SetActive(false);
        auto& lightScript=light.AddComponent<ObjectScript>();lightScript.scriptFile="light.cpp";
        ScriptDocument lightDoc=ScriptDocument::New();
        auto level=lightDoc.Add(0,NodeKind::Variable,"brightness");
        AtomicWrite(source.parent_path()/"light.cpp",lightDoc.Source());
        panel.Load(workspace,entry,source,false);
        auto children=panel.ChildSymbols(workspace,entry);
        Check(children.size()==1&&children[0].path=="Group/Light","Inactive grandchild was missing from Children");
        auto read=panel.BindRead(entry,main,entry.targets,children.front(),change);
        EditValue(entry,main,change,read);
        panel.Open(workspace,light,"ObjectScript");
        auto& lightEditor=panel.entries.back();
        auto value=*lightDoc.Get(level);
        Check(!panel.HasProtected(workspace,lightEditor,value),"An unsaved parent read promoted the variable");
        Check(panel.Save(workspace,entry,source),"Could not publish the parent script");
        Check(panel.HasProtected(workspace,lightEditor,value),"Published parent read did not promote the variable");
        middle.SetParent(&sibling);
        Check(!panel.HasProtected(workspace,lightEditor,value),"Reparented descendants retained an unrelated owner's grant");
        middle.SetParent(&object);
        Check(siblingScript.Bindings().empty(),"Child bindings leaked between components");
        auto preset=source.parent_path()/"copy.cpp";AtomicWrite(preset,main.document.Source());
        panel.Load(workspace,entry,preset,true);
        Check(entry.HasError()&&entry.targets.reads.empty(),"Unbound preset child was not reported immediately");
        auto diagnostic=std::find_if(entry.diagnostics.begin(),entry.diagnostics.end(),[&](const auto& d){
            return !d.warning&&d.node==change&&d.line==main.document.Get(change)->line;
        });
        Check(diagnostic!=entry.diagnostics.end(),"Unbound child diagnostic did not identify its source row");
        EditValue(entry,main,change,"1");
        Check(panel.Save(workspace,entry,source),"Could not publish the removal of a parent read");
        Check(!panel.HasProtected(workspace,lightEditor,value),"Last saved reference removal did not restore private visibility");
        auto invalid=source.parent_path()/"invalid.cpp";
        auto broken=main.document;broken.Add(0,NodeKind::Variable,"score");AtomicWrite(invalid,broken.Source());
        bool rejected=false;
        try{panel.Load(workspace,entry,invalid,false);}catch(const std::runtime_error&){rejected=true;}
        Check(rejected&&owner.scriptFile=="test-script.cpp"&&main.document.Source()==ReadFile(source),"Invalid Load File changed the active component or draft");
        auto policyDoc=main.document;auto field=*policyDoc.Get(var);
        field.riceCon=true;field.riceAccess="policy";field.policy="moderators";policyDoc.Edit(var,field);
        AtomicWrite(preset,policyDoc.Source());panel.Load(workspace,entry,preset,true);
        Check(main.document.Get(var)->policy.empty(),"Preset copied an external policy handle");
        Check(std::any_of(entry.diagnostics.begin(),entry.diagnostics.end(),[&](const auto& d){return !d.warning&&d.node==var;}),
              "Preset did not identify its unresolved policy");

        const auto ownerId=object.GetID();
        auto component=[&]{return workspace.editor.scene->Find(ownerId)->GetComponent<ObjectScript>();};
        workspace.editor.Play();
        panel.Load(workspace,entry,source.parent_path()/"light.cpp",false);
        Check(component()->scriptFile=="test-script.cpp","Load File changed the currently playing instance");
        workspace.editor.Stop();
        Check(component()->scriptFile=="light.cpp","Staged attachment was lost on Stop");
        workspace.editor.Undo();
        Check(component()->scriptFile=="test-script.cpp","Undo did not restore the old script attachment");
        workspace.editor.Redo();
        Check(component()->scriptFile=="light.cpp","Redo did not restore the staged attachment");

        ScriptPanel otherScenePanel;
        auto* currentObject=workspace.editor.scene->Find(ownerId);
        otherScenePanel.Open(workspace,*currentObject,"ObjectScript");
        auto& otherEntry=otherScenePanel.entries.back();
        auto method=First(otherEntry.main.document,NodeKind::Start);
        auto window=otherEntry.Separate(method);
        otherEntry.detachedTargets[window]=otherEntry.targetBases[window]=otherEntry.targets;
        auto& functionDraft=otherEntry.detached.at(window);
        auto newValue=functionDraft.document.Add(method,NodeKind::Variable,"closeDraft");
        otherEntry.Changed(functionDraft,newValue);
        Check(otherScenePanel.HasUnsavedChanges(workspace),"Detached-only RiceMonkey edits must prompt on project close");
        workspace.NewScene("Close target");
        auto active=workspace.project->ActiveScene();
        auto instance=workspace.editor.scene->GetInstanceID();
        otherScenePanel.SaveBeforeClose(workspace);
        Check(workspace.project->ActiveScene()==active&&workspace.editor.scene->GetInstanceID()==instance,
              "Saving a hidden script window must preserve the active scene");
        Check(!otherScenePanel.HasUnsavedChanges(workspace)&&ReadFile(source.parent_path()/"light.cpp").find("closeDraft")!=std::string::npos,
              "Hidden detached edits must reach their original script on Save");
        otherScenePanel.CloseProject();
        Check(otherScenePanel.entries.empty(),"Discarded or closed script windows must not reappear on reopen");
        auto shared=source.parent_path()/"light.cpp";
        auto beforeConflict=ReadFile(shared);
        for(int value:{5,7}){
            auto& owner=workspace.editor.scene->CreateGameObject(std::to_string(value));
            owner.AddComponent<ObjectScript>().scriptFile="light.cpp";
            otherScenePanel.Open(workspace,owner,"ObjectScript");
            auto& draft=otherScenePanel.entries.back();
            auto id=First(draft.main.document,NodeKind::Variable);
            EditValue(draft,draft.main,id,std::to_string(value));
        }
        bool conflict=false;
        try{otherScenePanel.SaveBeforeClose(workspace);}
        catch(const std::exception& error){conflict=std::string_view(error.what()).find("Conflicting edits")!=std::string_view::npos;}
        Check(conflict&&ReadFile(shared)==beforeConflict&&otherScenePanel.HasUnsavedChanges(workspace),
              "Competing drafts of one RiceMonkey source must stay open without overwriting the file");
    }
};
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Test directory required");
    Rice::ScriptPanelTestAccess::Run(Rice::fs::path(argv[1])/Rice::NewAssetID());
    std::cout<<"Script editor windows, instance navigation, merge, binding and save checks passed\n";
    return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
