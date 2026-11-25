#pragma once
#include "Rice/Benchmark.h"
#include "Project/SceneCodec.h"
#include <optional>
namespace Rice {
// Clipboard stores property values, never pointers to live components or resources.
class EditorClipboard {
    using Values=ComponentSnapshot;
    struct Node {EntityID source,parent;std::string name;Transform transform;bool active;std::vector<Values> components;bool isGroup;EntityID group,previousGroup;Matrix4 offset,bind,common,world;};
    std::vector<Node> objects;
    std::vector<Values> components;
    std::optional<Transform> componentTransform;
    Vector3 rootWorldPosition{0};
    uint64_t sourceScene=0;
public:
    bool HasObjects()const{ RICE_BENCHMARK_FUNCTION(); return !objects.empty();}
    bool HasComponents()const{ RICE_BENCHMARK_FUNCTION(); return componentTransform.has_value()||!components.empty();}
    void CopyObject(Scene& scene,EntityID id,ComponentRegistry& registry){
        RICE_BENCHMARK_FUNCTION();
        auto* root=scene.Find(id);if(!root)return;
        std::vector<Node> captured;
        auto capture=[&](GameObject& obj){ RICE_BENCHMARK_FUNCTION(); captured.push_back({obj.GetID(),obj.GetParent()?obj.GetParent()->GetID():0,obj.name,obj.transform,obj.activeSelf,ComponentSnapshot::CaptureAll(obj,registry),obj.isGroup,obj.GetGroup()?obj.GetGroup()->GetID():0,obj.previousGroup,obj.groupOffset,obj.groupParentBind,obj.groupCommonBind,obj.GetWorldMatrix()});};
        capture(*root);
        for(auto& obj:scene.GetObjects())if(obj.get()!=root&&!obj->IsPendingDestroy())for(auto* p=obj->GetHierarchyParent();p;p=p->GetHierarchyParent())if(p==root){capture(*obj);break;}
        objects=std::move(captured);sourceScene=scene.GetInstanceID();rootWorldPosition=Vector3(root->GetWorldMatrix()[3]);
    }
    void DeleteObject(EditorSession& editor){ RICE_BENCHMARK_FUNCTION(); if(editor.playing||!editor.scene->Find(editor.selected))return;auto id=editor.selected;editor.Edit([&](Scene& scene){ RICE_BENCHMARK_FUNCTION(); scene.Destroy(id);});editor.selected=0;}
    void CutObject(EditorSession& editor,ComponentRegistry& registry){ RICE_BENCHMARK_FUNCTION(); if(editor.playing||!editor.scene->Find(editor.selected))return;CopyObject(*editor.scene,editor.selected,registry);DeleteObject(editor);}
    EntityID PasteObject(EditorSession& editor,ComponentRegistry& registry){
        RICE_BENCHMARK_FUNCTION();
        if(editor.playing||objects.empty())return 0;EntityID pasted=0;
        editor.Edit([&](Scene& scene){ RICE_BENCHMARK_FUNCTION(); std::map<EntityID,EntityID> ids;std::vector<EntityID> created;
            try{
                for(auto& node:objects){auto& obj=scene.CreateGameObject(node.name);created.push_back(obj.GetID());ids[node.source]=obj.GetID();obj.transform=node.transform;obj.activeSelf=node.active;obj.isGroup=node.isGroup;ComponentSnapshot::Apply(obj,node.components,registry);}
                for(auto& node:objects){auto* obj=scene.Find(ids.at(node.source));if(ids.contains(node.parent))obj->SetParent(scene.Find(ids.at(node.parent)));else if(scene.GetInstanceID()==sourceScene)obj->SetParent(scene.Find(node.parent));}
                for(auto& node:objects)RemapScriptBindings(*scene.Find(ids.at(node.source)),[&](EntityID id){return ids.contains(id)?ids.at(id):EntityID{};});
                for(auto& node:objects)if(auto* anchor=scene.Find(ids.at(node.source))->GetComponent<Anchor>())anchor->child=ids.contains(anchor->child)?ids.at(anchor->child):0;
                for(auto& node:objects)if(auto* f=scene.Find(ids.at(node.source))->GetComponent<CameraFollower>())f->camera=ids.contains(f->camera)?ids.at(f->camera):f->camera;
                for(auto& node:objects){auto* obj=scene.Find(ids.at(node.source));auto* group=ids.contains(node.group)?scene.Find(ids.at(node.group)):(scene.GetInstanceID()==sourceScene?scene.Find(node.group):nullptr);
                    if(group){obj->SetGroup(group,false);obj->groupOffset=node.offset;obj->groupParentBind=node.bind;obj->groupCommonBind=node.common;}
                    obj->previousGroup=ids.contains(node.previousGroup)?ids.at(node.previousGroup):(scene.GetInstanceID()==sourceScene?node.previousGroup:0);
                }
                std::set<EntityID> placed;
                std::function<void(const Node&)> place=[&](const Node& node){ RICE_BENCHMARK_FUNCTION(); if(placed.contains(node.source))return;
                    for(auto dependency:{node.parent,node.group})if(ids.contains(dependency))for(auto& candidate:objects)if(candidate.source==dependency){place(candidate);break;}
                    scene.Find(ids.at(node.source))->SetWorldMatrix(node.world);placed.insert(node.source);
                };
                for(auto& node:objects)place(node);
                pasted=ids.at(objects.front().source);auto* root=scene.Find(pasted);root->TranslateWorld(rootWorldPosition-Vector3(root->GetWorldMatrix()[3]));
            }catch(...){for(auto id:created)scene.Destroy(id);scene.Flush();throw;}
        });editor.Select(pasted);return pasted;
    }
    void CopyComponent(GameObject& obj,const std::string& name,ComponentRegistry& registry){
        RICE_BENCHMARK_FUNCTION();
        if(name=="Transform"){componentTransform=obj.transform;components.clear();return;}
        auto* type=registry.Find(name);if(!type||!type->get(obj))return;
        auto value=ComponentSnapshot::Capture(*type,*type->get(obj));components={std::move(value)};componentTransform.reset();
    }
    void CopyAllComponents(GameObject& obj,ComponentRegistry& registry){ RICE_BENCHMARK_FUNCTION(); auto values=ComponentSnapshot::CaptureAll(obj,registry);components=std::move(values);componentTransform=obj.transform;}
    void PasteComponents(GameObject& obj,ComponentRegistry& registry){
        RICE_BENCHMARK_FUNCTION();
        if(!HasComponents())return;
        // Validate before changing the destination. Runtime channels/GPU handles are not copied.
        GameObject validation("Clipboard validation");ComponentSnapshot::Apply(validation,components,registry);
        ComponentSnapshot::Apply(obj,components,registry);if(componentTransform)obj.transform=*componentTransform;
    }
};
}
