#include "Scripting/ScriptAuthoring.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace Rice::Scripting;

namespace {
void Check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
ScriptId Find(const ScriptDocument& d,NodeKind kind,std::string name={}){
    for(const auto& n:d.Nodes())if(n.kind==kind&&(name.empty()||n.name==name))return n.id;
    return 0;
}
bool Has(const ScriptDocument& d,std::string text,bool warn=false){
    for(const auto& issue:d.Validate())if(issue.warning==warn&&issue.message.find(text)!=std::string::npos)return true;
    return false;
}
}
int main(int argc,char** argv){try{
    auto doc=ScriptDocument::New();
    auto start=Find(doc,NodeKind::Start),update=Find(doc,NodeKind::Update);
    Check(start&&update&&doc.Nodes().size()==2,"Lifecycle rows are editable, compiler glue stays hidden");
    auto score=doc.Add(0,NodeKind::Variable,"score");
    auto temp=doc.Add(start,NodeKind::Variable,"localOnly");
    Check(score&&temp&&doc.CanSee(score,update)&&!doc.CanSee(temp,update),"Start and Update local scopes are independent");
    auto f=doc.Add(0,NodeKind::Function,"Level");
    auto unused=doc.Add(0,NodeKind::Function,"Unused");
    Check(f&&unused&&Has(doc,"'Unused'",true),"Unused functions produce nonfatal diagnostics");
    auto fn=*doc.Get(f);fn.type="int";doc.Edit(f,fn);
    auto end=doc.Add(f,NodeKind::EndFunction);auto output=*doc.Get(end);output.value="2";doc.Edit(end,output);
    auto change=doc.Add(update,NodeKind::Change,"score");auto assignment=*doc.Get(change);
    assignment.value="score + 1";doc.Edit(change,assignment);
    auto branch=doc.Add(update,NodeKind::Condition);
    auto condition=*doc.Get(branch);condition.value="score == Level()";doc.Edit(branch,condition);
    Check(doc.Get(branch)->value=="score == Level()"&&doc.Text(branch).find("co_await Level()")!=std::string::npos,"Native calls in an expression preserve the visible expression");
    auto lamp=doc.Add(branch,NodeKind::Activate,"lamp");
    auto wait=doc.Add(start,NodeKind::Wait);
    auto pause=*doc.Get(wait);pause.value="0";doc.Edit(wait,pause);
    Check(lamp&&wait&&doc.Instances(score,update).size()==2&&doc.Instances(score,start).empty(),"Find respects scope and counts code locations");
    Check(!Has(doc,"'Level'",true),"Expression call is a use");
    auto original=doc;
    auto external=doc.Source();auto p=external.find("score + 1");external.replace(p,9,"score + 3");
    auto changedWait=*doc.Get(wait);changedWait.value="0.25";doc.Edit(wait,changedWait);
    doc.MergeExternal(original.Source(),external);
    Check(doc.Get(wait)->value=="0.25"&&doc.Get(change)->value=="score + 3","Independent source and window edits merge");
    auto wrong=*doc.Get(change);wrong.name="localOnly";doc.Edit(change,wrong);
    Check(Has(doc,"out-of-scope"),"Invalid assignment is diagnosed before native build");
    doc.Edit(change,assignment);
    auto duplicate=doc.Add(f,NodeKind::Variable,"score");Check(Has(doc,"Duplicate declaration"),"Script-wide duplicate names");
    doc.Erase(duplicate);
    auto list=doc.Add(0,NodeKind::Variable,"items");auto listDef=*doc.Get(list);
    listDef.type="std::vector<int>";listDef.value="{1, 2, 3}";doc.Edit(list,listDef);
    auto loop=doc.Add(start,NodeKind::Loop);
    auto loopDef=*doc.Get(loop);loopDef.value="items";doc.Edit(loop,loopDef);
    Check(doc.Add(loop,NodeKind::EndLoop)!=0,"Loop supports explicit exit");
    auto klass=doc.Add(0,NodeKind::Class,"Enemy");auto ctor=doc.Add(klass,NodeKind::OnCreate);
    auto dtor=doc.Add(klass,NodeKind::OnDestroy);
    Check(klass&&ctor&&dtor,"Optional constructors and destructors");
    auto dtorBranch=doc.Add(dtor,NodeKind::Condition);
    auto guard=*doc.Get(dtorBranch);guard.value="false";doc.Edit(dtorBranch,guard);
    auto dtorActivate=doc.Add(dtorBranch,NodeKind::Activate);
    Check(dtorActivate&&doc.Get(dtorBranch)->parent==dtor&&doc.Text(dtor).find("SafeDestroy([&]()")!=std::string::npos,"Destructor actions execute inside the SDK cancellation boundary");
    auto count=doc.Add(0,NodeKind::Variable,"constructed");
    auto ctorEffect=doc.Add(ctor,NodeKind::Change,"constructed");
    auto inc=*doc.Get(ctorEffect);inc.value="constructed + 1";doc.Edit(ctorEffect,inc);
    Check(count&&doc.Text(ctor).find("riceOwner->constructed")!=std::string::npos,"Class creation can change owning script state");
    auto held=doc.Add(ctor,NodeKind::Wait);
    Check(held&&Has(doc,"complete synchronously"),"Cannot suspend in a constructor");
    doc.Erase(held);
    auto construct=doc.Add(start,NodeKind::NewClass,"enemy");Check(construct,"Instance creation is an execution row");
    auto data=doc.Add(0,NodeKind::Class,"Position");doc.Add(data,NodeKind::Variable,"positionX");
    auto positions=doc.Add(0,NodeKind::Variable,"positions");auto values=*doc.Get(positions);
    values.type="std::vector<Position>";values.value="{}";doc.Edit(positions,values);
    Check(doc.CanRead(positions)&&doc.Source().find("std::tie(positionX)")!=std::string::npos,"Nested pure data classes support typed field transfer");
    auto state=doc.Add(0,NodeKind::State,"Movement");
    Check(state&&Find(doc,NodeKind::OnEnter)&&Find(doc,NodeKind::OnUpdate)&&Find(doc,NodeKind::OnExit),"State callbacks are nested lists");
    auto mode=doc.Add(start,NodeKind::SetState,"Movement");Check(mode,"Set initial state");
    auto rice=doc.Add(0,NodeKind::Variable,"lives");auto routed=*doc.Get(rice);
    routed.riceCon=true;routed.riceScope="global";routed.riceLife="stored";routed.riceAccess="policy";routed.policy="moderator";doc.Edit(rice,routed);
    ScriptDocument reopened;reopened.SetSource(doc.Source());
    Check(reopened.Get(rice)&&reopened.Get(rice)->policy=="moderator"&&reopened.Get(list)->value=="{1, 2, 3}","RiceCon and container initializer round trip");
    auto changedPolicy=doc.Source();auto policyAt=changedPolicy.find("\"moderator\"");
    changedPolicy.replace(policyAt,11,"\"owner\"");
    ScriptDocument riceExternal;riceExternal.SetSource(changedPolicy);
    Check(riceExternal.Get(rice)&&riceExternal.Get(rice)->policy=="owner","Externally edited native field descriptors override older view metadata");
    Check(reopened.Get(construct)&&reopened.Get(construct)->type=="Enemy","Class instance type survives reparse");
    Check(reopened.Text(construct).find("CheckedEmplace(enemy, *this)")!=std::string::npos&&reopened.Text(wait).find(std::to_string(wait)+"ULL")!=std::string::npos,"Safe class replacement and persistent Wait location");
    for(auto& issue:reopened.Validate())Check(issue.warning,issue.message.c_str());
    if(argc>1){std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<doc.Source();Check(bool(out),"Write generated test module");}
    auto renamed=*doc.Get(score);renamed.name="points";doc.Edit(score,renamed);
    Check(doc.Get(change)->name=="points"&&doc.Get(branch)->value.find("points")!=std::string::npos,"Variable rename preserves structured references");
    auto partial=*doc.Get(branch);partial.value="(";doc.Edit(branch,partial);
    Check(doc.Get(branch)&&doc.Get(branch)->value=="("&&Has(doc,"incomplete"),"Partially typed source keeps the active row");
    partial.value="points == 2";doc.Edit(branch,partial);Check(!Has(doc,"incomplete"),"Completing the expression restores the document");
    auto endBranch=doc.AddBranch(branch,NodeKind::Else);
    auto alt=doc.AddBranch(branch,NodeKind::ElseIf);
    Check(endBranch&&alt&&doc.Get(alt)->line<doc.Get(endBranch)->line&&doc.AddBranch(alt,NodeKind::Else)==endBranch,"A conditional has one final else");
    Check(doc.Move(alt,-1)&&doc.Get(alt)->kind==NodeKind::Condition&&doc.Get(branch)->kind==NodeKind::ElseIf,"Branch reordering preserves valid C++");
    auto target=ScriptDocument::New();
    Check(target.ImportClass(doc,klass)&&target.ImportClass(doc,klass),"Elsewhere class import has stable, independent declarations");
    auto nav=original;
    auto second=nav.Add(update,NodeKind::Wait);
    auto main=nav,detached=nav;
    Check(detached.Move(second,-1),"Detached reorder");
    main.MergeExternal(nav.Source(),detached.Source(),[&](auto id){return id==second;});
    Check(main.Get(second)->line<main.Get(branch)->line,"Apply imports detached order without resetting the window");
    auto binding=original;
    binding.Erase(score);
    auto recreated=binding.Add(0,NodeKind::Variable,"score");
    Check(recreated!=score&&Has(binding,"Unresolved reference: score"),"Recreating a name cannot capture a deleted backtick target");
    Check(binding.Instances(recreated).empty(),"Dangling references must not resolve in Find Instances");
    auto rebound=*binding.Get(change);rebound.references["score"]=recreated;binding.Edit(change,rebound);
    Check(binding.Instances(recreated).size()==1,"Explicit reselection binds the replacement declaration");
    std::string pasted=original.Source();
    auto raw=original.Text(wait);
    pasted.insert(pasted.find(raw),raw+"\n        ");
    ScriptDocument copied;copied.SetSource(pasted);
    Check(copied.Nodes().size()==original.Nodes().size()+1&&copied.Source()!=pasted&&copied.Get(wait),"Pasted duplicate annotations receive new persisted IDs");
    std::string noIds="#include \"Scripting/ScriptRuntime.h\"\nusing namespace Rice::Scripting;\nstruct Plain : ScriptBase {\nint points = 1;\nint Threshold() const { return 9; }\nTask<> Start(){ co_return; }\nTask<> Update(){ points += Threshold();\nLog(Severity::Info, R\"(////{raw} with a \" inside)\");\nco_return; }\n};\nRICE_SCRIPT_MODULE(Plain)";
    ScriptDocument plain;plain.SetSource(noIds);
    Check(plain.Source()==noIds&&Find(plain,NodeKind::Function,"Threshold")&&Find(plain,NodeKind::Change,"points")&&Find(plain,NodeKind::Custom),"Unannotated C++ recovers functions and operations without rewriting raw source");
    Check(plain.ScriptClassName()=="Plain"&&plain.HasModuleEntry()&&plain.FreeHookArity("Update")==-1,"Recognize an explicit module and its class hooks");
    auto plainVar=Find(plain,NodeKind::Variable,"points");Check(plain.Erase(plainVar),"Erase an unannotated native variable");
    plain.Add(0,NodeKind::Variable,"points");
    Check(Has(plain,"Unresolved reference: points"),"Deleting an unannotated declaration anchors existing uses against name capture");
    plain.SetSource("namespace Demo::Gameplay { struct Wrapped : Rice::Scripting::ScriptBase {}; } // RICE_SCRIPT_MODULE(Other)\n");
    Check(plain.ScriptClassName()=="Demo::Gameplay::Wrapped"&&!plain.HasModuleEntry(),"Implicit module adapter uses qualified names and skips comments");
    plain.SetSource("const char* text = \"RiceGetScriptApi()\"; void Start(){} inline void Update(float dt) noexcept {}");
    Check(!plain.HasModuleEntry()&&plain.ScriptClassName().empty()&&plain.FreeHookArity("Start")==0&&plain.FreeHookArity("Update")==1,"Staged adapters can identify synchronous free game hooks");
    const auto children=ScriptDocument::ChildReferences(R"test(// ReadChild<int>("stale", "score")
/* ReadChild<int>("wrong", "score") */
const char* example = R"(ReadChild<int>("stale-string", "unused"))";
auto actual = ReadChild<std::unordered_map<int, std::vector<float>>>("id\\\"key", R"tag(chi"ld)tag");
)test");
    Check(children.size()==1&&children.front().line==4&&children.front().key=="id\\\"key"&&children.front().name=="chi\"ld"&&
        ScriptDocument::SameType(children.front().type,"std::unordered_map<int,std::vector<float> >"),"Only live literal child reads grant access, including nested template types and escaped names");
    Check(doc.Erase(unused)&&doc.Get(score),"Only manually deleted declaration disappears");
    auto bad=doc.Source()+"\n{";
    doc.SetSource(bad);Check(doc.Source()==bad&&Has(doc,"delimiter"),"Broken external source preserved verbatim");
    std::string legacy="#define VALUE(x) do { x; } while(0)\nint original() { return 7; }\n";
    doc.SetSource(legacy);Check(doc.Source()==legacy&&doc.Nodes().size()==1,"Unrecognized C++ is preserved");
    std::cout<<"Script authoring checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
