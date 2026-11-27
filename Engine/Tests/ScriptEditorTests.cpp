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
    static void ArgumentInputs(Workspace& workspace){
        ScriptPanel panel;
        ScriptPanel::Entry entry;
        entry.Reset(ScriptDocument::New());
        auto& doc=entry.main.document;
        auto start=First(doc,NodeKind::Start);
        doc.Add(0,NodeKind::Variable,"score");
        auto function=doc.Add(0,NodeKind::Function,"Blend");
        auto definition=*doc.Get(function);
        definition.type="int";definition.args="int leftSample, int rightSample";
        Check(doc.Edit(function,definition),"Could not set up the two-parameter function");
        auto wait=doc.Add(start,NodeKind::Wait);
        auto row=*doc.Get(wait);row.value="score";doc.Edit(wait,row);
        auto strategy=doc.Add(0,NodeKind::Variable,"movement");
        auto slot=*doc.Get(strategy);slot.type="StrategySlot<void, int, int>";slot.value="{}";doc.Edit(strategy,slot);
        auto invoke=doc.Add(start,NodeKind::Custom);
        auto native=*doc.Get(invoke);native.value="co_await movement.Invoke(5);";doc.Edit(invoke,native);

        auto target=wait;
        std::string visible;
        auto frame=[&]{
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({620,500});
            ImGui::Begin("Argument input test",nullptr,ImGuiWindowFlags_NoSavedSettings);
            ImGui::LogToBuffer();
            auto line=*doc.Get(target);
            if(panel.ItemFields(workspace,entry,entry.main,entry.targets,line))panel.MarkEdit(entry,entry.main,line);
            visible=ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();ImGui::End();ImGui::Render();
        };
        auto inputFor=[&](const std::string& initial){
            for(const auto& [id,buffer]:panel.buffers)if(buffer.model==initial)return id;
            throw std::runtime_error("Missing argument input for "+initial);
        };
        auto enter=[&](ImGuiID id,const char* text){
            ImGui::ActivateItemByID(id);frame();
            Check(ImGui::GetActiveID()==id,"An argument field can receive keyboard input");
            auto& io=ImGui::GetIO();
            io.AddInputCharactersUTF8(text);frame();
            io.AddKeyEvent(ImGuiKey_Enter,true);frame();
            io.AddKeyEvent(ImGuiKey_Enter,false);frame();
        };
        frame();frame();
        const auto expression=inputFor("score");
        enter(expression,"Blend");
        Check(visible.find("leftSample")!=visible.npos&&visible.find("rightSample")!=visible.npos,
              "Typing a function name shows a field for each declared parameter");
        auto initialized=*doc.Get(wait);initialized.value="Blend(10, 20)";doc.Edit(wait,initialized);frame();
        enter(inputFor("20"),"score");
        ScriptDocument reopened;reopened.SetSource(doc.Source());
        auto call=ParseScriptCall(reopened.Get(wait)->value);
        Check(call&&call->name=="Blend"&&SplitArguments(call->arguments)==std::vector<std::string>{"10","score"},
              "Editing the second argument preserves the first argument and survives reopening");
        enter(expression,"score");
        Check(doc.Get(wait)->value=="score"&&visible.find("leftSample")==visible.npos&&visible.find("rightSample")==visible.npos,
              "Replacing a call with a scalar variable removes its parameter fields");

        target=doc.Add(start,NodeKind::Call,"Blend");
        auto direct=*doc.Get(target);direct.args="3";doc.Edit(target,direct);frame();
        definition=*doc.Get(function);definition.args="int leftSample, int rightSample, float bias = 1";
        doc.Edit(function,definition);frame();
        Check(visible.find("bias")!=visible.npos&&visible.find("rightSample")!=visible.npos&&doc.Get(target)->args=="3",
              "Adding a declared parameter expands an existing Call Function row without rewriting supplied arguments");
        definition=*doc.Get(function);definition.args="int leftSample";
        doc.Edit(function,definition);frame();
        Check(visible.find("leftSample")!=visible.npos&&visible.find("rightSample")==visible.npos&&
              visible.find("bias")==visible.npos&&doc.Get(target)->args=="3",
              "Removing parameters shrinks the Call Function row and leaves its first argument intact");

        target=doc.Add(start,NodeKind::Variable,"offset");
        auto vector=*doc.Get(target);vector.type="Vec3";vector.value="Vec3{1, 2, 3}";doc.Edit(target,vector);
        panel.buffers.clear();frame();
        enter(inputFor("2"),"score");
        reopened.SetSource(doc.Source());
        auto coordinates=ParseVectorInitializer(reopened.Get(target)->value,"Vec3");
        Check(coordinates&&SplitArguments(coordinates->arguments)==std::vector<std::string>{"1","score","3"},
              "Each component of a vector variable accepts an expression and preserves the other coordinates");
        target=doc.Add(start,NodeKind::Change,"score");
        panel.buffers.clear();frame();
        const auto targetName=inputFor("score");
        enter(targetName,"offset");
        auto savedVector=[&](const char* type){
            reopened.SetSource(doc.Source());
            auto value=ParseVectorInitializer(reopened.Get(target)->value,type);
            Check(value.has_value(),"Retargeting saves a native vector constructor");
            return SplitArguments(value->arguments);
        };
        Check(doc.Get(target)->name=="offset"&&visible.find("Z")!=visible.npos&&
              savedVector("Vec3")==std::vector<std::string>{"0","0","0"},
              "Typing a Vec3 target converts the default scalar into a three-coordinate value");
        enter(targetName,"score");
        Check(doc.Get(target)->name=="score"&&doc.Get(target)->value=="0"&&visible.find("Z")==visible.npos,
              "Typing a scalar target restores a single value field");
        auto planar=doc.Add(start,NodeKind::Variable,"planar");
        auto pair=*doc.Get(planar);pair.type="Vec2";pair.value="{}";doc.Edit(planar,pair);
        auto assignment=*doc.Get(target);assignment.value="7";doc.Edit(target,assignment);frame();
        enter(targetName,"planar");
        Check(savedVector("Vec2")==std::vector<std::string>{"7","0"},"Nonzero scalar survives as X of a Vec2");
        assignment=*doc.Get(target);assignment.value="Vec2{7, 8}";doc.Edit(target,assignment);frame();
        enter(targetName,"offset");
        Check(savedVector("Vec3")==std::vector<std::string>{"7","8","0"},"Vec2 to Vec3 retains X and Y and initializes Z");
        enter(targetName,"score");
        reopened.SetSource(doc.Source());
        Check(reopened.Get(target)->value=="7","Switching a constructed vector to a scalar retains its X value");
        assignment=*doc.Get(target);assignment.value="offset";doc.Edit(target,assignment);frame();
        enter(targetName,"offset");
        Check(doc.Get(target)->value=="offset","A compatible whole-vector expression survives a target change");

        target=invoke;panel.buffers.clear();
        const auto original=doc.Text(invoke);
        frame();frame();
        Check(panel.buffers.size()==2&&doc.Text(invoke)==original,
              "Opening a short native strategy call shows both inputs and preserves its source");
        const auto first=inputFor("5");
        const auto other=std::find_if(panel.buffers.begin(),panel.buffers.end(),[&](const auto& item){return item.first!=first;});
        enter(other->first,"score");
        reopened.SetSource(doc.Source());
        Check(SplitArguments(panel.ParseStrategy(*reopened.Get(invoke)).argument)==std::vector<std::string>{"5","score"},
              "Filling the missing strategy argument keeps the existing native argument");

        doc.SetSource(R"cpp(
            struct GameScript : ScriptBase {
                int score = 1;
                struct Pair {
                    struct Nested { /*rice:owner*/ GameScript* riceOwner = nullptr; };
                    Pair(GameScript&, int = 0, int = 0) {}
                };
                void Many(int first, ...) {}
                Task<> Start() {
                    std::optional<Pair> pair;
                    pair.emplace(*this);
                    CheckedEmplace(pair, *this, score, Compute(1, 2));
                    Many(score, Compute(1, 2));
                    co_await Missing(score, Compute(1, 2));
                    co_await missingSlot.Invoke(score, Compute(1, 2));
                }
            };
            RICE_SCRIPT_MODULE(GameScript)
        )cpp");
        std::vector<ScriptId> calls;
        for(const auto& item:doc.Nodes()){
            if(item.kind==NodeKind::Call||item.kind==NodeKind::NewClass||
               panel.ParseStrategy(item).action==ScriptPanel::StrategyAction::Invoke)calls.push_back(item.id);
        }
        Check(calls.size()==5,"Expected both native constructor forms, a variadic call, an unresolved call and a strategy invocation");
        for(auto id:calls){
            target=id;panel.buffers.clear();
            const auto before=doc.Source();
            frame();frame();
            if(doc.Get(id)->kind==NodeKind::NewClass&&doc.Get(id)->args=="*this"){
                Check(inputFor("*this")&&doc.Source()==before,"A native constructor's explicit owner is an editable argument");
                continue;
            }
            Check(doc.Source()==before&&visible.find("Add argument")!=visible.npos&&inputFor("Compute(1, 2)"),
                  "Opening a dynamic argument list preserves the source and keeps a nested call in one field");
            enter(inputFor("score"),"8");
            reopened.SetSource(doc.Source());
            const auto& item=*reopened.Get(id);
            const auto args=item.kind==NodeKind::Custom?panel.ParseStrategy(item).argument:item.args;
            std::vector<std::string> expected{"8","Compute(1, 2)"};
            if(item.kind==NodeKind::NewClass)expected.insert(expected.begin(),"*this");
            Check(SplitArguments(args)==expected,
                  "Editing one dynamic argument survives reopening and preserves its neighbor");
            if(item.name=="Missing"){
                const auto scope=ImGui::FindWindowByName("Argument input test")->GetID("Arguments");
                const auto list=ImHashStr("Arguments",0,scope);
                ImGui::ActivateItemByID(ImHashStr("Add argument",0,list));frame();frame();
                Check(SplitArguments(doc.Get(id)->args)==std::vector<std::string>{"8","Compute(1, 2)","0"},
                      "Adding an argument keeps the existing values in order");
                const int middle=1;
                ImGui::ActivateItemByID(ImHashStr("-##remove",0,ImHashData(&middle,sizeof(middle),list)));frame();frame();
                Check(SplitArguments(doc.Get(id)->args)==std::vector<std::string>{"8","0"},
                      "Removing the middle argument keeps its two neighbors");
            }
        }
    }
    static void ParameterDeclarations(Workspace& workspace){
        ScriptPanel panel;
        ScriptPanel::Entry entry;
        entry.Reset(ScriptDocument::New());
        auto& doc=entry.main.document;
        const auto function=doc.Add(0,NodeKind::Function,"Adjust");
        const auto klass=doc.Add(0,NodeKind::Class,"Counter");
        const auto method=doc.Add(klass,NodeKind::Function,"Reset");
        const std::string limits="std::pair<int, float> limits";
        const std::string count="int count = std::max(2, 3)";
        auto definition=*doc.Get(function);
        definition.args=limits+",  "+count;
        Check(doc.Edit(function,definition),"Could not set up an existing parameter declaration");
        definition=*doc.Get(method);
        definition.args="void";
        Check(doc.Edit(method,definition),"Could not set up a void parameter list");

        auto target=function;
        ImGuiID listId{};
        auto frame=[&]{
            ImGui::NewFrame();
            ImGui::SetNextWindowSize({620,500});
            ImGui::Begin("Parameter declaration test",nullptr,ImGuiWindowFlags_NoSavedSettings);
            listId=ImGui::GetID("Parameters");
            auto row=*doc.Get(target);
            if(panel.ItemFields(workspace,entry,entry.main,entry.targets,row))panel.MarkEdit(entry,entry.main,row);
            ImGui::End();ImGui::Render();
        };
        auto rowControl=[&](int index,const char* label){
            return ImHashStr(label,0,ImHashData(&index,sizeof(index),listId));
        };
        auto activate=[&](ImGuiID id){ImGui::ActivateItemByID(id);frame();};
        auto add=[&]{activate(ImHashStr("Add parameter",0,listId));};
        auto remove=[&](int index){activate(rowControl(index,"-##remove"));};

        const auto original=doc.Source();
        frame();frame();
        Check(doc.Source()==original&&panel.buffers.at(rowControl(0,"##declaration")).model==limits&&
              panel.buffers.at(rowControl(1,"##declaration")).model==count,
              "Opening a signature preserves its source and gives each declaration one complete row");
        add();
        const auto added=FunctionParameters(doc.Get(function)->args);
        Check(added.size()==3&&added.back().type=="int"&&!added.back().defaultValue.empty(),
              "Adding a parameter after a default creates a valid trailing integer declaration");
        const auto input=rowControl(2,"##declaration");
        activate(input);
        Check(ImGui::GetActiveID()==input,"The added parameter accepts keyboard input");
        const std::string scale="float scale = 1";
        auto& io=ImGui::GetIO();
        io.AddInputCharactersUTF8(scale.c_str());frame();
        io.AddKeyEvent(ImGuiKey_Enter,true);frame();
        io.AddKeyEvent(ImGuiKey_Enter,false);frame();
        Check(doc.Get(function)->args==limits+", "+count+", "+scale,
              "Editing a new declaration keeps the template and default expression in neighboring rows");
        remove(1);
        ScriptDocument reopened;reopened.SetSource(doc.Source());
        Check(reopened.Get(function)->args==limits+", "+scale,
              "Removing the middle parameter persists the other declarations in order");

        target=method;panel.buffers.clear();
        const auto beforeMethod=doc.Source();
        frame();
        Check(doc.Source()==beforeMethod&&!panel.buffers.contains(rowControl(0,"##declaration")),
              "Opening a method declared with (void) shows an empty parameter list and preserves its source");
        add();
        Check(FunctionParameters(doc.Get(method)->args).size()==1&&doc.Get(method)->args.find("void")==std::string::npos,
              "Adding the first class method parameter replaces (void)");
        remove(0);
        reopened.SetSource(doc.Source());
        Check(reopened.Get(method)->args.empty()&&reopened.Get(function)->args==limits+", "+scale,
              "Removing the last class method parameter saves an empty signature without changing the other function");
    }
    static void ExternalMoveCoordinates(){
        ScriptDocument disk=ScriptDocument::New();
        auto update=First(disk,NodeKind::Update);
        auto move=disk.Add(update,NodeKind::Move);
        ScriptEditingSession session;
        session.Reset(disk);
        auto window=session.Separate(update);
        auto& detached=session.detached.at(window);
        auto editX=[&](ScriptDocument& doc,const std::string& x){
            auto row=*doc.Get(move);row.moveCoordinates[0]=x;
            Check(doc.Edit(move,row),"Could not edit Move's X coordinate");
        };
        editX(detached.document,"2");session.Changed(detached,move);
        editX(session.main.document,"4");session.Changed(session.main,move);
        auto fileTime=session.main.editedAt.at(move)-std::chrono::seconds(1);
        detached.editedAt[move]=fileTime-std::chrono::seconds(1);
        editX(disk,"3");session.External(disk.Source(),fileTime);
        Check(session.main.document.Get(move)->moveCoordinates[0]=="4"&&
              detached.document.Get(move)->moveCoordinates[0]=="3"&&!detached.edits.contains(move),
              "A coordinate-only file edit must preserve the newer main draft and replace the older detached draft");
    }
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
        ExternalMoveCoordinates();
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
        ArgumentInputs(workspace);
        ParameterDeclarations(workspace);
        SignalInputs(workspace);
        HeaderWindows(workspace);
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
    static void SignalInputs(Workspace& workspace){
        ScriptPanel panel;
        ScriptPanel::Entry entry;
        entry.Reset(ScriptDocument::New());
        auto& view=entry.main;
        auto& doc=view.document;
        auto update=First(doc,NodeKind::Update);
        Check(panel.Title(*doc.Get(update))=="Every Frame","The Update hook uses the Every Frame label");
        auto trigger=doc.Add(update,NodeKind::Trigger);
        auto event=doc.Add(trigger,NodeKind::Event,"Arrived");
        auto sibling=doc.Add(update,NodeKind::Trigger);
        Check(trigger&&event&&sibling,"Can author two triggers and a local event");
        Check(panel.Title(*doc.Get(trigger)).find("When single")!=std::string::npos,
              "A new trigger displays single in its header");
        auto target=trigger;
        ImGuiID scope{};
        auto frame=[&]{
            ImGui::NewFrame();
            ImGui::SetNextWindowSize({600,300});
            ImGui::Begin("Signal input test");
            scope=ImGui::GetCurrentWindow()->ID;
            auto row=*doc.Get(target);
            if(panel.ItemFields(workspace,entry,view,entry.targets,row))panel.MarkEdit(entry,view,row);
            ImGui::End();ImGui::Render();
        };
        frame();frame();
        auto activate=[&](const char* name){ImGui::ActivateItemByID(ImHashStr(name,0,scope));frame();frame();};
        activate("###frequency");
        Check(doc.Get(trigger)->args=="every","The frequency button switches from single to every");
        auto chooseSource=[&](const char* label){
            activate("##source");
            const auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;
            Check(!popups.empty()&&popups.back().Window,"The trigger source picker opens");
            ImGui::ActivateItemByID(popups.back().Window->GetID(label));frame();frame();
        };
        chooseSource("Game Seconds");
        Check(doc.Get(trigger)->type=="Game Seconds"&&doc.Get(trigger)->value=="1",
              "Choosing Game Seconds provides a default duration");
        activate("##duration");
        auto& io=ImGui::GetIO();
        io.AddInputCharactersUTF8("0.25f");frame();
        io.AddKeyEvent(ImGuiKey_Enter,true);frame();
        io.AddKeyEvent(ImGuiKey_Enter,false);frame();
        chooseSource("Seconds");
        Check(doc.Get(trigger)->type=="Seconds"&&doc.Get(trigger)->value=="0.25f"&&doc.Get(trigger)->args=="every",
              "Switching clocks keeps the edited duration and frequency");
        chooseSource("Collision Start");
        Check(doc.Get(trigger)->type=="Collision Start"&&doc.Get(trigger)->value.empty(),
              "Switching to a collision trigger clears the duration");
        activate("block others");
        Check(doc.Get(trigger)->blockOthers&&panel.UpdateBlockers(workspace,entry,doc,update).contains(trigger),
              "A blocking Every Frame trigger dims the following actions in the edit preview");
        activate("block others");
        Check(panel.UpdateBlockers(workspace,entry,doc,update).empty(),"Clearing block others restores the following rows");
        auto signal=*doc.Get(sibling);signal.type="Event";signal.value=SignalKey(*doc.Get(event));
        Check(panel.SignalPreview(workspace,view,signal,NodeKind::Event)=="Missing event",
              "A sibling trigger outside a private event's scope cannot pick it");
        auto registering=*doc.Get(trigger);registering.type="Event";registering.value=signal.value;
        Check(panel.SignalPreview(workspace,view,registering,NodeKind::Event)=="Missing event",
              "A trigger source is selected in the enclosing scope, before its local declarations");
        target=event;frame();activate("private");
        signal.value=SignalKey(*doc.Get(event));
        Check(doc.Get(event)->isPublic&&panel.SignalPreview(workspace,view,signal,NodeKind::Event).ends_with("Arrived"),
              "Making an event public exposes its qualified display name to the sibling trigger");
    }
    static void HeaderWindows(Workspace& workspace){
        ScriptPanel panel;
        auto& entry=panel.entries.emplace_back();
        entry.project=workspace.project->root;entry.scene=workspace.project->ActiveScene();
        auto doc=ScriptDocument::New();
        auto klass=doc.Add(0,NodeKind::Class,"Walker");
        auto method=doc.Add(klass,NodeKind::Function,"Step");
        auto signature=*doc.Get(method);
        signature.args="float distance";
        doc.Edit(method,signature);
        auto local=doc.Add(method,NodeKind::Variable,"speed");
        auto wait=doc.Add(method,NodeKind::Wait);
        entry.Reset(std::move(doc));

        auto frame=[&]{ImGui::NewFrame();panel.Draw(workspace,{});ImGui::Render();};
        auto hover=[&](ImGuiWindow* window,ImGuiID item,bool rightEdge=false){
            const auto rect=window->InnerClipRect;
            const float x=rightEdge?window->WorkRect.Max.x-ImGui::GetFrameHeight()*.5f:
                                    std::min(rect.Min.x+70,rect.GetCenter().x);
            for(float y=rect.Min.y+2;y<rect.Max.y;y+=4){
                ImGui::GetIO().AddMousePosEvent(x,y);frame();
                if(ImGui::GetCurrentContext()->HoveredId==item)return;
            }
            throw std::runtime_error("Missing header or menu item in "+std::string(window->Name));
        };
        auto click=[&](ImGuiMouseButton button){
            ImGui::GetIO().AddMouseButtonEvent(button,true);frame();
            ImGui::GetIO().AddMouseButtonEvent(button,false);frame();frame();
        };
        auto windowFor=[&](unsigned id){
            const auto& view=id?entry.detached.at(id):entry.main;
            auto* window=ImGui::FindWindowByName(panel.WindowLabel(entry,view,id).c_str());
            Check(window&&window->Active,"The scoped RiceMonkey window is visible");
            return window;
        };
        auto separate=[&](unsigned from,ScriptId root,std::initializer_list<std::string> groups){
            auto* window=windowFor(from);
            Check(!window->DC.ChildWindows.empty(),"The script content is visible");
            auto* content=window->DC.ChildWindows.front();
            auto seed=content->ID;
            for(const auto& group:groups)seed=ImHashStr(group.c_str(),0,seed);
            seed=ImHashStr(std::to_string(root).c_str(),0,seed);
            hover(content,ImHashStr("###item",0,seed));
            click(ImGuiMouseButton_Right);
            const auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;
            Check(!popups.empty()&&popups.back().Window,"Right-clicking the declaration header opens its menu");
            auto* menu=popups.back().Window;
            hover(menu,menu->GetID("Separate Window"));
            const auto count=entry.detached.size();
            click(ImGuiMouseButton_Left);
            Check(entry.detached.size()==count+1&&entry.detached.at(entry.nextWindow).root==root,
                  "Separate Window opens one editor rooted at the clicked declaration");
            windowFor(entry.nextWindow);
            return entry.nextWindow;
        };
        frame();frame();
        auto* content=windowFor(0)->DC.ChildWindows.front();
        const auto group=ImHashStr("Class Editor",0,content->ID);
        const auto openKey=ImHashStr("open",0,group);
        hover(content,ImHashStr("##expand",0,group),true);
        click(ImGuiMouseButton_Left);
        Check(!content->StateStorage.GetBool(openKey,true),"The section triangle collapses the class list");
        hover(content,ImHashStr("Class Editor",0,group));
        click(ImGuiMouseButton_Left);
        Check(content->StateStorage.GetBool(openKey,false),"The section title reopens the class list");
        auto classWindow=separate(0,klass,{"Class Editor"});
        auto& classDraft=entry.detached.at(classWindow);
        EditValue(entry,classDraft,wait,"-1");
        frame();
        auto methodWindow=separate(classWindow,method,{std::to_string(klass),"Function Editor"});
        auto& methodDraft=entry.detached.at(methodWindow);
        Check(methodDraft.document.Get(wait)->value=="-1"&&
              std::any_of(methodDraft.diagnostics.begin(),methodDraft.diagnostics.end(),[&](const auto& issue){return !issue.warning&&issue.node==wait;}),
              "A function detached from a class draft inherits that draft's edits and diagnostics");
        EditValue(entry,methodDraft,wait,"2");
        auto* methodEditor=windowFor(methodWindow);
        hover(methodEditor,methodEditor->GetID("Apply Changes"));click(ImGuiMouseButton_Left);
        Check(entry.main.document.Get(wait)->value=="2"&&classDraft.document.Get(wait)->value=="-1",
              "Clicking Apply Changes updates the main draft and keeps the class window's independent draft");

        auto* body=methodEditor->DC.ChildWindows.front();
        auto left=[&](ImGuiID id){
            ImGui::FocusWindow(body);
            ImGui::SetNavID(id,ImGuiNavLayer_Main,body->NavRootFocusScopeId,ImRect{});
            frame();
            Check(ImGui::GetCurrentContext()->NavIdIsAlive,"The expanded method submits the requested control");
            return body->NavRectRel[ImGuiNavLayer_Main].Min.x;
        };
        const auto root=ImHashStr(std::to_string(method).c_str(),0,body->ID);
        const auto variables=ImHashStr("Variables",0,root);
        const auto localRow=ImHashStr(std::to_string(local).c_str(),0,variables);
        const auto actionRow=ImHashStr(std::to_string(wait).c_str(),0,root);
        const int first=0;
        const auto parameterRow=ImHashData(&first,sizeof(first),ImHashStr("Parameters",0,root));
        const float headingX=left(ImHashStr("###item",0,root));
        const float sectionX=left(ImHashStr("Variables",0,variables));
        const float localX=left(ImHashStr("###item",0,localRow));
        const float actionX=left(ImHashStr("###item",0,actionRow));
        const float parameterX=left(ImHashStr("-##remove",0,parameterRow));
        Check(sectionX>headingX&&localX>sectionX&&actionX==localX&&parameterX>sectionX,
              "A method indents its sections and nests its parameters, local variables and actions under them");
    }
};
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Test directory required");
    Rice::ScriptPanelTestAccess::Run(Rice::fs::path(argv[1])/Rice::NewAssetID());
    std::cout<<"Script editor windows, instance navigation, merge, binding and save checks passed\n";
    return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
