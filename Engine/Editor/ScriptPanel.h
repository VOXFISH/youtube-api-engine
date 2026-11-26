#pragma once
#include "Editor/ScriptEditSession.h"
#include "Project/Workspace.h"
#include "Platform/FileDialog.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <charconv>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <list>
#include <regex>
#include <sstream>

namespace Rice {

inline const char* MonkeyScriptKind(std::string_view type){
    if(type=="ObjectScript")return "Object";
    if(type=="BlankScript")return "Blank Script";
    if(type=="SingletonManager")return "Singleton Manager";
    return nullptr;
}

// A window key describes a serialized component, not its address; Play, Stop,
// prefab restore and Undo can replace every C++ object in the scene.
class ScriptPanel {
    using Doc=Scripting::ScriptDocument;
    using View=Scripting::ScriptView;
    using Node=Scripting::ScriptNode;
    using Kind=Scripting::NodeKind;
    using Id=Scripting::ScriptId;
    using Targets=Scripting::ScriptTargets;
    struct Entry : Scripting::ScriptEditingSession {
        unsigned serial{};
        bool attached=true;
        fs::path project,scene,path;
        EntityID entity{};
        std::string component, error;
        fs::file_time_type stamp{};
        std::uintmax_t fileSize{};
        std::map<unsigned,Targets> detachedTargets,targetBases;
    };
    friend struct ScriptPanelTestAccess;
    std::list<Entry> entries;
    unsigned nextSerial{};
    bool focused=false;
    double nextPoll=0;
    struct CachedFile { fs::file_time_type time{}; std::uintmax_t size{}; Doc doc; };
    std::map<fs::path,CachedFile> files;
    std::map<fs::path,std::map<std::string,fs::path>> publications;
    std::map<fs::path,std::set<std::pair<std::string,std::string>>> publishedReads;

    static std::string Utf8(const fs::path& path){
        auto value=path.generic_u8string();
        return {value.begin(),value.end()};
    }
    static fs::path Path(const std::string& link){
        return fs::path(std::u8string(link.begin(),link.end()));
    }
    static std::string ComponentLabel(const std::string& type){
        if(auto kind=MonkeyScriptKind(type))return std::string("Monkey Script (")+kind+")";
        return std::string(ComponentRegistry::CanonicalName(type));
    }
    static bool InScene(const Workspace& w,const Entry& entry){
        return entry.project==w.project->root&&entry.scene==w.project->ActiveScene();
    }
    static ScriptComponentBase* Owner(Workspace& w,const Entry& entry){
        if(!InScene(w,entry))return nullptr;
        auto* object=w.editor.scene->Find(entry.entity);
        auto* type=w.registry.Find(entry.component);
        return object&&type?static_cast<ScriptComponentBase*>(type->get(*object)):nullptr;
    }
    static Targets Bindings(const ScriptComponentBase& owner){
        Targets out;
        out.children=owner.Bindings();
        std::istringstream reads(owner.childReads);
        std::string line;
        while(std::getline(reads,line)){
            std::istringstream fields(line);std::string key,type,name;Id symbol{};
            if(fields>>std::quoted(key)>>std::quoted(type)>>std::quoted(name)){
                fields>>symbol;out.reads[key]={type,name,symbol};
            }
        }
        return out;
    }
    static void WriteBindings(ScriptComponentBase& owner,const Targets& targets){
        std::ostringstream reads;
        owner.SetBindings(targets.children);
        for(auto& [key,read]:targets.reads)
            reads<<std::quoted(key)<<' '<<std::quoted(read.component)<<' '<<std::quoted(read.variable)<<' '<<read.symbol<<'\n';
        owner.childReads=reads.str();
    }
    static std::string Under(GameObject* object,EntityID ancestor){
        std::vector<std::string> names;
        while(object&&object->GetID()!=ancestor){
            names.push_back(object->name);
            object=object->GetHierarchyParent();
        }
        if(!object||names.empty())return {};
        std::string path;
        for(auto it=names.rbegin();it!=names.rend();++it){
            if(!path.empty())path+='/';
            path+=*it;
        }
        return path;
    }
    const Doc* DiskDocument(const fs::path& file){
        std::error_code ec;
        auto stamp=fs::last_write_time(file,ec);
        if(ec)return nullptr;
        auto size=fs::file_size(file,ec);
        if(ec)return nullptr;
        auto it=files.find(file);
        if(it==files.end()||it->second.time!=stamp||it->second.size!=size){
            CachedFile item;item.time=stamp;item.size=size;
            try{item.doc.SetSource(ReadFile(file));}catch(const std::exception&){return nullptr;}
            it=files.insert_or_assign(file,std::move(item)).first;
        }
        return &it->second.doc;
    }
    static void Stamp(Entry& entry){
        std::error_code ec;
        entry.stamp=fs::last_write_time(entry.path,ec);
        entry.fileSize=fs::file_size(entry.path,ec);
    }
    bool PublishedRead(Workspace& w,const ScriptComponentBase& owner,const std::string& key,const std::string& name){
        if(owner.scriptFile.empty())return false;
        auto [list,load]=publications.try_emplace(w.project->root);
        if(load)for(auto& [file,module]:Scripting::PublishedScripts(w.project->root))
            list->second[file]=module.binary;
        auto path=Scripting::ScriptSourcePath(w.project->root,Path(owner.scriptFile));
        auto found=list->second.find(Scripting::ScriptKey(w.project->root,path));
        if(found==list->second.end())return false;
        auto [reads,scan]=publishedReads.try_emplace(found->second);
        if(scan){
            auto source=found->second;source.replace_extension(".cpp");
            if(auto* doc=DiskDocument(source))
                for(auto& ref:Doc::ChildReferences(doc->Source()))reads->second.emplace(ref.key,ref.name);
        }
        return reads->second.contains({key,name});
    }
    bool HasProtected(Workspace& w,const Entry& entry,const Node& node){
        if(node.parent!=0)return false;
        auto* object=w.editor.scene->Find(entry.entity);
        for(auto* p=object?object->GetHierarchyParent():nullptr;p;p=p->GetHierarchyParent()){
            for(auto& type:w.registry.types)if(MonkeyScriptKind(type.name)){
                if(auto* owner=static_cast<ScriptComponentBase*>(type.get(*p))){
                    auto targets=Bindings(*owner);
                    for(auto& [key,read]:targets.reads)
                        if(read.variable==node.name&&(!read.symbol||read.symbol==node.id)&&read.component==entry.component&&
                           owner->BoundChild(key)==entry.entity&&PublishedRead(w,*owner,key,node.name))return true;
                }
            }
        }
        return false;
    }
    struct ChildSymbol {
        EntityID entity;
        std::string path,component,name,type;
        Id symbol;
        Kind kind;
        fs::path file;
    };
    std::vector<ChildSymbol> ChildSymbols(Workspace& w,const Entry& entry,bool includeTypes=false){
        std::vector<ChildSymbol> result;
        for(auto& object:w.editor.scene->GetObjects()){
            auto path=Under(object.get(),entry.entity);
            if(path.empty()||object->IsPendingDestroy()||object->IsPoolAvailable())continue;
            for(auto& type:w.registry.types)if(MonkeyScriptKind(type.name)){
                auto* owner=static_cast<ScriptComponentBase*>(type.get(*object));
                if(!owner||owner->scriptFile.empty())continue;
                auto source=Scripting::ScriptSourcePath(w.project->root,Path(owner->scriptFile));
                auto* doc=DiskDocument(source);
                if(!doc)continue;
                for(auto& n:doc->Nodes()){
                    if(n.parent)continue;
                    if(n.kind!=Kind::Variable&&(!includeTypes||(n.kind!=Kind::Class&&(n.kind!=Kind::Function||!n.isPublic))))continue;
                    if(!includeTypes&&!doc->CanRead(n.id))continue;
                    result.push_back({object->GetID(),path,type.name,n.name,n.kind==Kind::Class?n.name:n.type,n.id,n.kind,source});
                }
            }
        }
        return result;
    }
    std::string BindRead(Entry& entry,View& view,Targets& bindings,const ChildSymbol& child,Id row){
        if(auto* childDoc=DiskDocument(child.file)){
            for(auto& cls:childDoc->Nodes())if(!cls.parent&&cls.kind==Kind::Class&&child.type.find(cls.name)!=std::string::npos&&
                view.document.ImportClass(*childDoc,cls.id)){
                for(auto& n:view.document.Nodes())if(n.kind==Kind::Class&&n.name==cls.name)entry.Changed(view,n.id);
            }
        }
        auto prefix="read_"+std::to_string(row)+"_";
        std::string key;
        for(auto& [candidate,read]:bindings.reads)if(candidate.starts_with(prefix)&&
            read.component==child.component&&read.variable==child.name&&read.symbol==child.symbol&&bindings.children[candidate]==child.entity){
            key=candidate;break;
        }
        if(key.empty())key=prefix+NewAssetID();
        bindings.children[key]=child.entity;
        bindings.reads[key]={child.component,child.name,child.symbol};
        return "ReadChild<"+child.type+">(\""+key+"\", \""+child.name+"\")";
    }
    using ReadRef=Scripting::ScriptChildReference;
    struct CachedReads {std::string source;std::vector<ReadRef> refs;};
    std::map<const View*,CachedReads> cachedReads;
    const std::vector<ReadRef>& Reads(const View& view){
        auto& cache=cachedReads[&view];
        if(cache.source!=view.document.Source()){
            cache.source=view.document.Source();cache.refs=Doc::ChildReferences(cache.source);
        }
        return cache.refs;
    }
    const char* ReadProblem(Workspace& w,const Entry& e,const Targets& targets,const ReadRef& ref){
        auto read=targets.reads.find(ref.key);
        auto target=targets.children.find(ref.key);
        auto* child=target==targets.children.end()?nullptr:w.editor.scene->Find(target->second);
        if(read==targets.reads.end()||read->second.variable!=ref.name||!child||Under(child,e.entity).empty())
            return "Choose a child for ";
        auto* type=w.registry.Find(read->second.component);
        auto* attachment=type?dynamic_cast<ScriptComponentBase*>(type->get(*child)):nullptr;
        const auto* doc=attachment&&!attachment->scriptFile.empty()?DiskDocument(Scripting::ScriptSourcePath(w.project->root,Path(attachment->scriptFile))):nullptr;
        if(!doc||std::none_of(doc->Nodes().begin(),doc->Nodes().end(),[&](const Node& n){
            return !n.parent&&n.kind==Kind::Variable&&n.name==ref.name&&n.type==ref.type&&
                   (!read->second.symbol||n.id==read->second.symbol);
        }))return "Reconnect the changed or deleted child declaration for ";
        return nullptr;
    }
    static void CommitLink(Workspace& w,Entry& e,const fs::path& path,const Targets& targets){
        if(!e.attached)return;
        auto link=Scripting::ScriptKey(w.project->root,path);
        auto* owner=Owner(w,e);
        if(!w.editor.playing&&owner&&owner->scriptFile==link&&Bindings(*owner)==targets)return;
        w.editor.EditScriptAttachment(e.entity,e.component,[&](ScriptComponentBase& attachment){
            attachment.scriptFile=link;WriteBindings(attachment,targets);
        });
    }
    static void LogIssue(Workspace& w,Entry& e,const std::string& text){
        e.error=text;
        w.Log(text,spdlog::level::err);
    }
    void Load(Workspace& w,Entry& e,const fs::path& path,bool preset){
        auto source=ReadFile(path);
        Doc next;next.SetSource(source);
        if(source.empty()||(!next.HasScriptBody()&&next.Nodes().empty()))
            throw std::runtime_error("This file contains no script source.");
        if(preset){
            auto rows=next.Nodes();
            for(auto n:rows)if(n.kind==Kind::Variable&&n.riceCon&&n.riceAccess=="policy"&&!n.policy.empty()){
                n.policy.clear();next.Edit(n.id,n);
            }
            e.main.document=std::move(next);
            e.targets={};ValidateTargets(w,e,e.targets);
            auto now=fs::file_time_type::clock::now();
            for(auto& n:e.main.document.Nodes()){
                e.main.edits[n.id]=++e.revision;e.main.editedAt[n.id]=now;
            }
            e.compiledSource.clear();e.error.clear();
        }else{
            for(auto& d:next.Validate())if(!d.warning)
                throw std::runtime_error("Could not load this script. Line "+std::to_string(d.line)+"  "+d.message);
            Targets bindings=e.path==path?e.savedTargets:Targets{};
            CommitLink(w,e,path,bindings);
            if(path!=e.path&&!e.detached.empty()){
                // Open function windows continue to own the previous document.
                // A later Apply in one must never write into an unrelated file.
                auto& old=entries.emplace_back(e);
                old.serial=++nextSerial;old.attached=false;old.main.open=false;
                old.detached=std::move(e.detached);
                old.detachedTargets=std::move(e.detachedTargets);old.targetBases=std::move(e.targetBases);
            }
            e.path=path;e.Reset(std::move(next),std::move(bindings));Stamp(e);
            ValidateTargets(w,e,e.targets);
            e.error.clear();
        }
    }
    void ValidateTargets(Workspace& w,Entry& e,Targets& targets){
        e.diagnostics=e.main.document.Validate();
        std::set<std::string> used,reads;
        for(const auto& row:e.main.document.Nodes())if(row.kind==Kind::Activate){
            auto* object=w.editor.scene->Find(e.entity);
            if(!row.name.empty()){
                used.insert(row.name);auto found=targets.children.find(row.name);
                object=found!=targets.children.end()?w.editor.scene->Find(found->second):nullptr;
                if(!object||Under(object,e.entity).empty()){
                    e.diagnostics.push_back({row.id,row.line,false,"Choose a child in this component's scene."});continue;
                }
            }
            auto* type=row.type.empty()?nullptr:w.registry.Find(row.type);
            if(!row.type.empty()&&(!type||!object||!type->get(*object)))
                e.diagnostics.push_back({row.id,row.line,false,"Choose a component attached to this target."});
        }
        for(auto& ref:Reads(e.main)){
            used.insert(ref.key);reads.insert(ref.key);
            if(auto problem=ReadProblem(w,e,targets,ref))
                e.diagnostics.push_back({e.main.document.AtLine(ref.line),ref.line,false,std::string(problem)+ref.name+"."});
        }
        static const std::regex active(R"rx(\bActive\s*\(\s*"([^"]+)")rx");
        const auto& source=e.main.document.Source();
        for(std::sregex_iterator it(source.begin(),source.end(),active),end;it!=end;++it){
            const std::string key=(*it)[1];used.insert(key);
            auto target=targets.children.find(key);
            if(target!=targets.children.end()&&!Under(w.editor.scene->Find(target->second),e.entity).empty())continue;
            std::size_t line=1+std::count(source.begin(),source.begin()+it->position(),'\n');
            e.diagnostics.push_back({e.main.document.AtLine(line),line,false,"Choose a child for this active-state condition."});
        }
        std::erase_if(targets.reads,[&](const auto& pair){return !reads.contains(pair.first);});
        std::erase_if(targets.children,[&](const auto& pair){return !used.contains(pair.first);});
    }
    bool Save(Workspace& w,Entry& e,const fs::path& path,bool poll=true){
        if(poll)Poll(e);
        auto targets=e.targets;
        ValidateTargets(w,e,targets);
        if(e.HasError()){
            for(auto& d:e.diagnostics)if(!d.warning)w.Log(d.message,spdlog::level::err);
            return false;
        }
        if(e.attached)w.editor.EditScriptAttachment(e.entity,e.component);
        try{w.SaveScript(path,e.main.document.Source());}
        catch(const std::exception& ex){
            e.error=ex.what();
            // MSVC reports file(line[,column]); Clang and GCC use file:line:column.
            static const std::regex at(R"((?:\(|:)([0-9]+)(?:,[0-9]+\)|\)|:[0-9]+):?\s*(?:fatal )?error)",std::regex::icase);
            std::istringstream details(e.error);std::string line;
            while(std::getline(details,line)){
                std::smatch match;
                if(std::regex_search(line,match,at)){
                    auto number=static_cast<std::size_t>(std::stoul(match[1]));
                    e.diagnostics.push_back({e.main.document.AtLine(number),number,false,line});
                }
            }
            w.Log(e.error,spdlog::level::err);
            return false;
        }
        CommitLink(w,e,path,targets);
        publications.erase(w.project->root);
        e.path=path;e.diskSource=e.compiledSource=e.main.document.Source();
        e.savedTargets=e.targets=std::move(targets);
        e.error.clear();Stamp(e);
        return true;
    }
    bool SaveDialog(Workspace& w,Entry& e,bool as){
        auto path=e.path;
        if(path.empty()||as){
            auto selected=PickScriptDestination(path.empty()?w.project->root/"Scripts"/"Untitled.cpp":path);
            if(!selected)return false;
            path=*selected;
        }
        return Save(w,e,path);
    }
    void Poll(Entry& e){
        if(e.path.empty())return;
        std::error_code ec;
        auto stamp=fs::last_write_time(e.path,ec);
        if(ec){e.error="The linked C++ file is unavailable.";return;}
        auto size=fs::file_size(e.path,ec);
        if(ec||stamp==e.stamp&&size==e.fileSize)return;
        auto source=ReadFile(e.path);
        e.External(std::move(source),stamp);e.stamp=stamp;e.fileSize=size;e.error.clear();
    }
    void Toolbar(Workspace& w,Entry& e,const std::string& ide){
        if(!ImGui::BeginMenuBar())return;
        auto guard=[&](auto action){try{action();}catch(const std::exception& ex){LogIssue(w,e,ex.what());}};
        if(ImGui::BeginMenu("Save")){
            if(ImGui::MenuItem("Save","Ctrl+S"))guard([&]{SaveDialog(w,e,false);});
            if(ImGui::MenuItem("Save As..."))guard([&]{SaveDialog(w,e,true);});
            if(ImGui::MenuItem("Save as Preset"))guard([&]{
                if(e.path.empty()||e.Dirty()||!Scripting::SourceMatchesPublication(w.project->root,e.path,e.main.document.Source())){
                    w.Log("Save this script before creating a preset.",spdlog::level::warn);return;
                }
                if(auto dest=PickScriptDestination(e.path.parent_path()/Path(Utf8(e.path.stem())+" Preset.cpp"),true))
                    AtomicWrite(*dest,e.main.document.Source());
            });
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Load")){
            for(bool preset:{false,true})if(ImGui::MenuItem(preset?"Load Preset...":"Load File..."))guard([&]{
                if(auto path=PickScriptFile(w.project->root/"Scripts"))Load(w,e,*path,preset);
            });
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Open in C++"))guard([&]{
            if(e.path.empty()){w.Log("Save this script to choose its C++ file before opening the IDE.",spdlog::level::info);return;}
            OpenScriptFile(e.path,Path(ide));
        });
        ImGui::EndMenuBar();
    }

    // Widget implementations use immutable node snapshots while the document
    // reparses individual changes. A child index or C++ address is never a UI ID.
#include "ScriptPanelWidgets.inl"
#include "ScriptStrategyWidgets.inl"

    static bool DetachedChanged(const Entry& e,unsigned id,const View& view){
        return view.open&&(view.document.Source()!=view.base||e.detachedTargets.at(id)!=e.targetBases.at(id));
    }
    static bool HasDraft(const Entry& e){
        if(e.Dirty())return true;
        for(const auto& [id,view]:e.detached)if(DetachedChanged(e,id,view))return true;
        return false;
    }
    static std::string MergeCloseSources(const std::string& base,const std::string& local,const std::string& other,const fs::path& path){
        if(local==base||local==other)return other;
        if(other==base)return local;
        Doc first,second;
        first.SetSource(local);first.MergeExternal(base,other);
        second.SetSource(other);second.MergeExternal(base,local);
        if(first.Source()!=second.Source())
            throw std::runtime_error("Conflicting edits to "+Utf8(path)+". Go back and resolve the script edits before closing.");
        return first.Source();
    }
public:
    bool HasUnsavedChanges(const Workspace& w)const{
        for(const auto& entry:entries)if(entry.project==w.project->root&&HasDraft(entry))return true;
        return false;
    }
    void SaveBeforeClose(Workspace& w){
        struct Source {fs::path path;std::optional<std::string> disk;std::string merged;};
        struct Draft {Entry* original;Entry candidate;size_t source;};
        std::vector<Source> sources;
        std::vector<Draft> drafts;
        for(auto& entry:entries){
            if(entry.project!=w.project->root||!HasDraft(entry))continue;
            auto candidate=entry;
            for(auto& [id,view]:candidate.detached)if(DetachedChanged(candidate,id,view))ApplyWindow(candidate,id);
            auto path=entry.path;
            if(path.empty()){
                auto selected=PickScriptDestination(w.project->root/"Scripts"/"Untitled.cpp");
                if(!selected)throw std::runtime_error("The RiceMonkey script was not saved.");
                path=*selected;
            }
            path=fs::weakly_canonical(path);
            auto existing=std::find_if(sources.begin(),sources.end(),[&](const Source& source){
                std::error_code ec;return path==source.path||fs::equivalent(path,source.path,ec);
            });
            if(existing==sources.end()){
                auto disk=fs::is_regular_file(path)?std::optional(ReadFile(path)):std::nullopt;
                if(!entry.path.empty()&&!disk)throw std::runtime_error("The linked RiceMonkey file is missing: "+Utf8(path));
                sources.push_back({path,disk,disk.value_or(entry.diskSource)});
                existing=std::prev(sources.end());
            }
            const auto& base=entry.path.empty()&&existing->disk?*existing->disk:entry.diskSource;
            existing->merged=MergeCloseSources(base,candidate.main.document.Source(),existing->merged,path);
            drafts.push_back({&entry,std::move(candidate),size_t(existing-sources.begin())});
        }
        for(auto& draft:drafts){
            auto& entry=*draft.original;auto& candidate=draft.candidate;auto& source=sources[draft.source];
            // Other scenes can retain hidden RiceMonkey windows. Apply their
            // component bindings to an isolated copy, even while Play is running.
            Rice::Scope<Workspace> inactive;
            if(!InScene(w,entry)){
                inactive=CreateScope<Workspace>(w.project->FilePath(),true,[&](auto& registry){registry=w.registry;});
                inactive->editor.Replace(w.project->LoadScene(inactive->registry,entry.scene));
            }
            auto& context=inactive?*inactive:w;
            auto disk=fs::is_regular_file(source.path)?std::optional(ReadFile(source.path)):std::nullopt;
            if(disk!=source.disk)throw std::runtime_error("The RiceMonkey file changed while closing: "+Utf8(source.path));
            candidate.main.document.SetSource(source.merged);
            bool saved=false;
            try{saved=Save(context,candidate,source.path,false);}
            catch(...){cachedReads.erase(&candidate.main);throw;}
            cachedReads.erase(&candidate.main);
            if(!saved){
                if(!candidate.error.empty())throw std::runtime_error(candidate.error);
                if(candidate.HasError())throw std::runtime_error("Resolve the RiceMonkey script errors before saving.");
                throw std::runtime_error("The RiceMonkey script was not saved.");
            }
            if(inactive)w.project->SaveInactiveScene(entry.scene,*inactive->editor.scene,inactive->registry);
            source.disk=candidate.diskSource;
            for(const auto& [id,view]:entry.detached)cachedReads.erase(&view);
            entry=std::move(candidate);
        }
    }
    void CloseProject(){
        entries.clear();cachedReads.clear();files.clear();publications.clear();publishedReads.clear();focused=false;
    }
    bool Focused()const{return focused;}
    static const std::map<uint64_t,std::string>& RuntimeErrors(const Workspace& w,const fs::path& path){
        static const std::map<uint64_t,std::string> empty;
        if(path.empty()||w.runtimeScriptErrors.empty())return empty;
        auto it=w.runtimeScriptErrors.find(Scripting::ScriptKey(w.project->root,path));
        return it==w.runtimeScriptErrors.end()?empty:it->second;
    }
    bool HasError(Workspace& w,EntityID entity,const std::string& type)const{
        auto* object=w.editor.scene->Find(entity);auto* meta=w.registry.Find(type);
        auto* attachment=object&&meta?dynamic_cast<ScriptComponentBase*>(meta->get(*object)):nullptr;
        if(!attachment)return false;
        auto path=attachment->scriptFile.empty()?fs::path{}:Scripting::ScriptSourcePath(w.project->root,Path(attachment->scriptFile));
        if(!RuntimeErrors(w,path).empty())return true;
        for(auto& e:entries)if(e.attached&&InScene(w,e)&&e.entity==entity&&e.component==type){
            if(w.editor.playing||e.path==path)return e.HasError()||!e.error.empty();
        }
        return false;
    }
    void Open(Workspace& w,GameObject& object,const std::string& type){
        auto* metadata=w.registry.Find(type);
        auto* component=metadata?static_cast<ScriptComponentBase*>(metadata->get(object)):nullptr;
        if(!component)return;
        auto path=component->scriptFile.empty()?fs::path{}:Scripting::ScriptSourcePath(w.project->root,Path(component->scriptFile));
        for(auto& e:entries)if(e.attached&&InScene(w,e)&&e.entity==object.GetID()&&e.component==type){
            if(w.editor.playing||e.path==path){
                if(!w.editor.playing&&e.targets==e.savedTargets)e.targets=e.savedTargets=Bindings(*component);
                e.main.open=e.main.focus=true;return;
            }
            e.attached=false;
        }
        auto& e=entries.emplace_back();e.serial=++nextSerial;
        e.project=w.project->root;e.scene=w.project->ActiveScene();e.entity=object.GetID();e.component=type;e.path=path;
        auto doc=Doc::New();
        try{if(!path.empty()){doc.SetSource(ReadFile(path));Stamp(e);}}
        catch(const std::exception& ex){LogIssue(w,e,ex.what());}
        e.Reset(std::move(doc),Bindings(*component));
    }
    void Draw(Workspace& w,const std::string& ide){
        focused=false;
        const bool poll=ImGui::GetTime()>=nextPoll;
        if(poll){nextPoll=ImGui::GetTime()+0.2;publications.clear();}
        for(auto& e:entries){
            if(!InScene(w,e))continue;
            try{
                if(poll&&(e.main.open||!e.detached.empty()))Poll(e);
                if(e.main.open)DrawWindow(w,e,e.main,0,ide);
                for(auto& [id,view]:e.detached)
                    if(view.open)DrawWindow(w,e,view,id,ide);
                std::erase_if(e.detached,[&](const auto& item){
                    if(item.second.open)return false;
                    cachedReads.erase(&item.second);e.detachedTargets.erase(item.first);e.targetBases.erase(item.first);return true;
                });
            }catch(const std::exception& ex){LogIssue(w,e,ex.what());}
        }
    }
};
} // namespace Rice
