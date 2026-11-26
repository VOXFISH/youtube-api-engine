#pragma once
#include "Rice/Benchmark.h"
#include "Project/SceneCodec.h"
#include "Project/SceneLayers.h"
#include "GizmoMath.h"
namespace Rice {
class EditorGroups {
public:
    static EntityID Create(EditorSession& editor,const std::set<EntityID>& selected,EntityID forcedParent=0){
        RICE_BENCHMARK_FUNCTION();
        if(editor.playing)return 0;
        std::vector<GameObject*> roots;
        for(auto id:selected)if(auto* obj=editor.scene->Find(id)){
            if(obj->GetComponent<SceneLayer>())return 0;
            bool covered=false;for(auto candidate:selected)if(candidate!=id)if(auto* ancestor=editor.scene->Find(candidate);ancestor&&obj->DependsOn(ancestor)){covered=true;break;}
            if(!covered)roots.push_back(obj);
        }
        if(roots.empty())return 0;
        auto* parent=forcedParent?editor.scene->Find(forcedParent):roots.front()->GetHierarchyParent();
        if(!forcedParent)for(auto* obj:roots)if(obj->GetHierarchyParent()!=parent){parent=nullptr;break;}
        if(parent&&std::abs(glm::determinant(parent->GetWorldMatrix()))<1e-8f)throw std::runtime_error("Cannot group under singular parent");
        Vector3 low(1e30f),high(-1e30f);
        for(auto* obj:roots){auto [a,b]=GizmoMath::SpaceBounds(*obj,Matrix4(1));
            if(std::abs(glm::determinant(obj->GetWorldMatrix()))<1e-8f)throw std::runtime_error("Cannot group singular transform");
            low=glm::min(low,a);high=glm::max(high,b);}
        EntityID result=0;
        editor.Edit([&](Scene& scene){ RICE_BENCHMARK_FUNCTION(); auto& group=scene.CreateGameObject();result=group.GetID();group.isGroup=true;
            group.SetParent(parent);SceneLayers::PlaceRoot(group,roots.front());group.SetWorldMatrix(glm::translate(Matrix4(1),(low+high)*.5f));
            for(auto* obj:roots){auto previous=obj->GetGroup();obj->SetGroup(&group);obj->previousGroup=previous?previous->GetID():0;}
        });editor.Select(result);return result;
    }
    static EntityID ControlChildren(EditorSession& editor,EntityID id){
        RICE_BENCHMARK_FUNCTION();
        std::set<EntityID> children;for(auto& obj:editor.scene->GetObjects())if(!obj->IsPendingDestroy()&&obj->GetHierarchyParent()&&obj->GetHierarchyParent()->GetID()==id)children.insert(obj->GetID());
        return Create(editor,children,id);
    }
    static void Ungroup(EditorSession& editor,EntityID id){
        RICE_BENCHMARK_FUNCTION();
        auto* group=editor.scene->Find(id);if(editor.playing||!group||!group->isGroup||group->GetComponent<SceneLayer>())return;
        std::vector<EntityID> released;
        editor.Edit([&](Scene& scene){
            RICE_BENCHMARK_FUNCTION();
            // Capture all matrices before modifying any dependency.
            std::vector<std::pair<GameObject*,Matrix4>> members;
            for(auto& obj:scene.GetObjects())if(!obj->IsPendingDestroy()&&(obj->GetGroup()==group||obj->GetParent()==group))members.push_back({obj.get(),obj->GetWorldMatrix()});
            for(auto& [obj,world]:members){
                if(obj->GetGroup()==group){auto* previous=scene.Find(obj->previousGroup);obj->SetGroup(previous==group?nullptr:previous,false);obj->previousGroup=0;}
                if(obj->GetParent()==group)obj->SetParent(group->GetHierarchyParent());
                SceneLayers::PlaceRoot(*obj,group->GetHierarchyParent());
                obj->SetWorldMatrix(world);released.push_back(obj->GetID());
            }
            scene.Destroy(id);
        });editor.Select(0);for(auto member:released)editor.Select(member,true);
    }
    static void InsertSibling(EditorSession& editor,EntityID source,EntityID reference,bool after){
        RICE_BENCHMARK_FUNCTION();
        if(editor.playing||source==reference)return;
        auto* obj=editor.scene->Find(source);auto* sibling=editor.scene->Find(reference);if(!obj||!sibling)return;
        if(!CanPlaceBeside(*obj,*sibling))return;
        auto* parent=sibling->GetHierarchyParent();
        if(parent&&parent->DependsOn(obj))throw std::invalid_argument("Hierarchy cycle");
        editor.Edit([&](Scene& scene){ RICE_BENCHMARK_FUNCTION(); if(obj->GetHierarchyParent()!=parent)Reparent(*obj,parent);scene.PlaceSibling(source,reference,after);});
    }
    static bool CanPlaceBeside(const GameObject& object,const GameObject& sibling){
        const bool layer=object.GetComponent<SceneLayer>()!=nullptr;
        return layer==(sibling.GetComponent<SceneLayer>()!=nullptr)&&
               (!layer||(!object.IsRequiredLayer()&&!sibling.IsRequiredLayer()));
    }
    static void Reparent(GameObject& obj,GameObject* parent){
        RICE_BENCHMARK_FUNCTION();
        if(parent&&obj.GetComponent<SceneLayer>())throw std::invalid_argument("Layers must be at the scene root");
        if(parent&&parent->DependsOn(&obj))throw std::invalid_argument("Hierarchy cycle");
        if(parent&&std::abs(glm::determinant(parent->GetWorldMatrix()))<1e-8f)throw std::runtime_error("Singular parent transform");
        std::vector<std::pair<GameObject*,Matrix4>> members;
        if(obj.isGroup)for(auto& member:obj.GetScene().GetObjects())if(member->GetGroup()==&obj)members.push_back({member.get(),member->GetWorldMatrix()});
        auto world=obj.GetWorldMatrix();
        if(parent&&parent->isGroup){auto previous=obj.GetGroup();obj.SetGroup(parent);obj.previousGroup=previous?previous->GetID():0;}
        else{obj.SetGroup(nullptr,false);obj.previousGroup=0;obj.SetParent(parent);obj.SetWorldMatrix(world);}
        for(auto& [member,matrix]:members){member->SetGroup(&obj,false);member->SetWorldMatrix(matrix);}
    }
};
}
