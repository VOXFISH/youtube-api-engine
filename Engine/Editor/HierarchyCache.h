#pragma once
#include "Scene/Scene.h"
#include <set>
#include <unordered_map>

namespace Rice {
// Structural relationships are read once per scene revision. Drawing and
// selection changes use this adjacency index instead of scanning the scene.
class HierarchyCache {
public:
    struct Row {GameObject* object=nullptr;EntityID id=0;unsigned depth=0;size_t subtreeEnd=0;};
private:
    struct Node {GameObject* object=nullptr;EntityID parent=0;std::vector<EntityID> children;};
    uint64_t instance=0,revision=0,rebuilds=0,flattenings=0;
    std::unordered_map<EntityID,Node> nodes;
    std::vector<EntityID> roots;
    std::set<EntityID> expanded,selection;
    EntityID selected=0;
    std::vector<Row> rows;
    std::unordered_map<EntityID,size_t> positions;
    bool rowsDirty=true;

    void Rebuild(Scene& scene){
        RICE_BENCHMARK_FUNCTION();
        nodes.clear();roots.clear();nodes.reserve(scene.GetObjectCount());
        std::vector<EntityID> order;order.reserve(scene.GetObjectCount());
        for(const auto& object:scene.GetObjects())if(!object->IsPendingDestroy()&&!object->IsPoolAvailable()){
            auto id=object->GetID();auto* parent=object->GetHierarchyParent();
            nodes.emplace(id,Node{object.get(),parent?parent->GetID():0,{}});order.push_back(id);
        }
        for(auto id:order){auto& node=nodes.at(id);auto parent=nodes.find(node.parent);
            if(parent!=nodes.end())parent->second.children.push_back(id);else roots.push_back(id);
        }
        std::erase_if(expanded,[&](EntityID id){return !nodes.contains(id);});
        revision=scene.GetHierarchyRevision();++rebuilds;rowsDirty=true;
    }
    void RevealSelection(){
        RICE_BENCHMARK_FUNCTION();
        auto reveal=[&](EntityID id){
            auto found=nodes.find(id);
            while(found!=nodes.end()&&found->second.parent){
                auto parent=found->second.parent;if(expanded.insert(parent).second)rowsDirty=true;
                found=nodes.find(parent);
            }
        };
        for(auto id:selection)reveal(id);
        if(selected&&!selection.contains(selected))reveal(selected);
    }
    void Flatten(){
        RICE_BENCHMARK_FUNCTION();
        rows.clear();positions.clear();
        std::vector<std::pair<EntityID,unsigned>> pending;
        for(auto it=roots.rbegin();it!=roots.rend();++it)pending.push_back({*it,0});
        while(!pending.empty()){
            auto [id,depth]=pending.back();pending.pop_back();auto& node=nodes.at(id);
            positions.emplace(id,rows.size());rows.push_back({node.object,id,depth,0});
            if(expanded.contains(id))for(auto it=node.children.rbegin();it!=node.children.rend();++it)pending.push_back({*it,depth+1});
        }
        std::vector<size_t> ancestors;
        for(size_t i=0;i<rows.size();++i){
            while(!ancestors.empty()&&rows[ancestors.back()].depth>=rows[i].depth){rows[ancestors.back()].subtreeEnd=i;ancestors.pop_back();}
            ancestors.push_back(i);
        }
        for(auto index:ancestors)rows[index].subtreeEnd=rows.size();
        ++flattenings;rowsDirty=false;
    }
public:
    void Sync(Scene& scene,const std::set<EntityID>& nextSelection,EntityID nextSelected){
        RICE_BENCHMARK_FUNCTION();
        bool replaced=instance!=scene.GetInstanceID();
        if(replaced){instance=scene.GetInstanceID();expanded.clear();selection.clear();selected=0;}
        bool changed=replaced||revision!=scene.GetHierarchyRevision();
        if(changed)Rebuild(scene);
        if(changed||selection!=nextSelection||selected!=nextSelected){
            selection=nextSelection;selected=nextSelected;RevealSelection();
        }
        if(rowsDirty)Flatten();
    }
    const std::vector<Row>& Rows()const{return rows;}
    bool HasChildren(EntityID id)const{auto found=nodes.find(id);return found!=nodes.end()&&!found->second.children.empty();}
    bool Expanded(EntityID id)const{return expanded.contains(id);}
    void SetExpanded(EntityID id,bool value){
        if(value){if(expanded.insert(id).second)rowsDirty=true;}
        else if(expanded.erase(id))rowsDirty=true;
    }
    std::optional<size_t> Position(EntityID id)const{auto found=positions.find(id);return found==positions.end()?std::nullopt:std::optional<size_t>(found->second);}
    uint64_t RebuildCount()const{return rebuilds;}
    uint64_t FlattenCount()const{return flattenings;}
};
}
