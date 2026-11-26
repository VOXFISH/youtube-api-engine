#pragma once

#include "Scripting/ScriptAuthoring.h"
#include <algorithm>
#include <filesystem>
#include <optional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Rice::Scripting {

// Each window has its own selection and draft. Revisions measure the time an
// item was actually edited, including edits in a window that is still detached.
struct ScriptView {
    ScriptDocument document;
    ScriptId root{}, selected{}, reveal{};
    bool scroll{}, findOpen{}, open=true, focus=true;
    std::map<ScriptId,bool> expanded;
    std::map<ScriptId,std::uint64_t> edits;
    std::map<ScriptId,std::filesystem::file_time_type> editedAt;
    std::map<ScriptId,std::map<std::string,ScriptId>> selectedReferences;
    std::vector<ScriptId> matches;
    std::vector<ScriptDiagnostic> diagnostics;
    std::size_t match{};
    std::string base, search, variableQuery;
    ScriptId searchingVariablesAt{};

    void Reveal(ScriptId id){
        selected=reveal=id;
        scroll=true;
        for(auto* node=document.Get(id);node;node=document.Get(node->parent))expanded[node->id]=true;
    }
    void Find(ScriptId symbol){
        matches=document.Instances(symbol,root);
        match=0;
        if(!matches.empty())Reveal(matches.front());
    }
    void Navigate(int step){
        if(matches.empty())return;
        if(step<0&&match>0)--match;
        if(step>0&&match+1<matches.size())++match;
        Reveal(matches[match]);
    }
};

struct ScriptChildRead {
    std::string component,variable;
    ScriptId symbol{};
    bool operator==(const ScriptChildRead&)const=default;
};

// Per-component target IDs live in the scene, never in the shared C++ file.
struct ScriptTargets {
    std::map<std::string,std::uint64_t> children;
    std::map<std::string,ScriptChildRead> reads;
    bool operator==(const ScriptTargets&)const=default;
};

struct ScriptEditingSession {
    ScriptView main;
    ScriptTargets targets,savedTargets;
    std::map<unsigned,ScriptView> detached;
    std::string diskSource,compiledSource;
    std::vector<ScriptDiagnostic> diagnostics;
    std::uint64_t revision{};
    unsigned nextWindow=0;

    bool Dirty()const{return main.document.Source()!=diskSource||targets!=savedTargets;}

    void Reset(ScriptDocument document,ScriptTargets bindings={}){
        diskSource=document.Source();
        compiledSource.clear();
        main.document=std::move(document);
        main.selected=0;
        main.expanded.clear();
        main.edits.clear();
        main.editedAt.clear();
        main.matches.clear();
        targets=savedTargets=std::move(bindings);
        diagnostics=main.document.Validate();
    }
    unsigned Separate(ScriptId root){
        auto id=++nextWindow;
        auto& view=detached[id];
        view.document=main.document;
        view.root=root;
        view.base=main.document.Source();
        view.diagnostics=main.document.Validate();
        view.expanded[root]=true;
        return id;
    }
    void Changed(ScriptView& view,ScriptId node){
        view.edits[node]=++revision;
        view.editedAt[node]=std::filesystem::file_time_type::clock::now();
        if(&view==&main)diagnostics=main.document.Validate();
        else view.diagnostics=view.document.Validate();
    }
    void Apply(ScriptView& view){
        // A later Apply click does not turn an old item edit into a new edit.
        main.document.MergeExternal(view.base,view.document.Source(),[&](ScriptId id){
            auto edit=view.edits.find(id);
            return edit!=view.edits.end()&&edit->second>=main.edits[id];
        });
        for(auto [id,time]:view.edits)if(time>=main.edits[id]){
            main.edits[id]=time;main.editedAt[id]=view.editedAt[id];
        }
        view.document=main.document;
        view.base=main.document.Source();
        view.edits.clear();
        view.editedAt.clear();
        view.diagnostics=diagnostics=main.document.Validate();
    }
    void External(std::string source,std::optional<std::filesystem::file_time_type> modified={}){
        if(source==diskSource)return;
        const auto time=++revision;
        ScriptDocument before,after;
        before.SetSource(diskSource);after.SetSource(source);
        std::set<ScriptId> touched;
        auto changed=[&](const ScriptNode& n){
            auto* old=before.Get(n.id);
            return !old||old->parent!=n.parent||old->kind!=n.kind||old->name!=n.name||old->type!=n.type||
                   old->value!=n.value||old->args!=n.args||old->moveCoordinates!=n.moveCoordinates||
                   old->riceCon!=n.riceCon||old->isPublic!=n.isPublic||
                   old->riceScope!=n.riceScope||old->riceLife!=n.riceLife||old->riceAccess!=n.riceAccess||
                   old->policy!=n.policy||old->references!=n.references;
        };
        for(auto& row:after.Nodes())if(changed(row))touched.insert(row.id);
        for(auto& row:before.Nodes())if(!after.Get(row.id))touched.insert(row.id);
        if(!after.HasScriptBody()||!before.HasScriptBody())touched.insert(0);
        auto apply=[&](ScriptView& view){
            auto previous=view.document.Source();
            view.document.MergeExternal(diskSource,source);
            auto keep=[&](ScriptId id){return modified&&view.editedAt.contains(id)&&view.editedAt[id]>*modified;};
            if(modified)view.document.MergeExternal(diskSource,previous,[&](ScriptId id){return touched.contains(id)&&keep(id);});
            for(auto id:touched)if(!keep(id)){
                if(&view==&main){
                    view.edits[id]=time;
                    view.editedAt[id]=modified.value_or(std::filesystem::file_time_type::clock::now());
                }else{view.edits.erase(id);view.editedAt.erase(id);}
            }
        };
        apply(main);
        for(auto& [window,view]:detached){
            apply(view);
            ScriptDocument base;
            base.SetSource(view.base);
            base.MergeExternal(diskSource,source);
            view.base=base.Source();
            view.diagnostics=view.document.Validate();
        }
        diskSource=std::move(source);
        diagnostics=main.document.Validate();
    }
    void Discard(){
        main.document.SetSource(diskSource);
        main.edits.clear();
        main.editedAt.clear();
        targets=savedTargets;
        diagnostics=main.document.Validate();
    }
    bool HasError()const{
        return std::any_of(diagnostics.begin(),diagnostics.end(),[](const auto& diagnostic){return !diagnostic.warning;});
    }
};

} // namespace Rice::Scripting
