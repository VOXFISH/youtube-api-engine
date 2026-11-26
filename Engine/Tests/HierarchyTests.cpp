#include "Editor/SceneInspector.h"
#include <iostream>
using namespace Rice;

namespace Rice {
struct SceneInspectorTestAccess {
    static void Hierarchy(SceneInspector& inspector,Workspace& workspace){inspector.DrawHierarchy(workspace);}
    static size_t Drawn(const SceneInspector& inspector){return inspector.hierarchyRowsDrawn;}
    static uint64_t Rebuilds(const SceneInspector& inspector){return inspector.hierarchy.RebuildCount();}
    static uint64_t Flattenings(const SceneInspector& inspector){return inspector.hierarchy.FlattenCount();}
    static void Expand(SceneInspector& inspector,EntityID id,bool open){inspector.hierarchy.SetExpanded(id,open);}
    static size_t Rows(const SceneInspector& inspector){return inspector.hierarchy.Rows().size();}
    static EntityID Popup(const SceneInspector& inspector){return inspector.hierarchyPopupObject;}
};
}
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::vector<EntityID> IDs(const HierarchyCache& cache){std::vector<EntityID> ids;for(const auto& row:cache.Rows())ids.push_back(row.id);return ids;}

void CacheLifecycle(){
    Scene scene;HierarchyCache cache;std::set<EntityID> selection;
    auto root=scene.CreateGameObject("Root").GetID();auto child=scene.CreateGameObject("Child").GetID();
    auto other=scene.CreateGameObject("Other").GetID();scene.Find(child)->SetParent(scene.Find(root));
    cache.Sync(scene,selection,0);Check(IDs(cache)==std::vector<EntityID>({root,other}),"Collapsed hierarchy contains roots in scene order");
    auto builds=cache.RebuildCount(),flats=cache.FlattenCount(),revision=scene.GetHierarchyRevision();
    for(int frame=0;frame<120;++frame)cache.Sync(scene,selection,0);
    Check(cache.RebuildCount()==builds&&cache.FlattenCount()==flats,"Idle frames neither rebuild nor traverse the flattened hierarchy");
    scene.Find(root)->name="Renamed";scene.Find(root)->transform.position.x=3;scene.Find(child)->SetParent(scene.Find(root));
    cache.Sync(scene,selection,0);Check(scene.GetHierarchyRevision()==revision&&cache.RebuildCount()==builds&&cache.Rows()[0].object->name=="Renamed","Rename/transform and unchanged parent do not rebuild cached hierarchy");
    selection.insert(child);cache.Sync(scene,selection,child);
    Check(IDs(cache)==std::vector<EntityID>({root,child,other})&&cache.Rows()[1].depth==1,"Selecting a hidden child reveals its ancestors");
    Check(cache.RebuildCount()==builds,"Selection only updates visible rows, not adjacency");
    cache.SetExpanded(root,false);cache.Sync(scene,selection,child);
    Check(IDs(cache)==std::vector<EntityID>({root,other}),"A selected branch can be manually collapsed");
    cache.SetExpanded(root,true);cache.Sync(scene,selection,child);
    scene.Find(child)->SetParent(scene.Find(other));cache.Sync(scene,selection,child);
    Check(IDs(cache)==std::vector<EntityID>({root,other,child})&&cache.Rows().back().depth==1,"Reparent invalidates adjacency and reveals the new ancestor");
    scene.PlaceSibling(other,root,false);cache.Sync(scene,selection,child);
    Check(IDs(cache)==std::vector<EntityID>({other,child,root}),"Sibling insertion updates cached order");
    scene.Find(other)->SetPoolState(true,true);cache.Sync(scene,selection,child);
    Check(IDs(cache)==std::vector<EntityID>({root}),"Pooled available roots and descendants are omitted");
    scene.Find(other)->SetPoolState(true,false);cache.Sync(scene,selection,child);
    Check(cache.Rows().size()==3,"Pool acquisition brings hierarchy objects back");
    auto group=scene.CreateGameObject("Group").GetID();scene.Find(group)->isGroup=true;
    scene.Find(child)->SetGroup(scene.Find(group));cache.Sync(scene,selection,child);
    Check(cache.Rows().back().id==child&&cache.Rows().back().depth==1,"Group membership updates visible ancestry");
    scene.Destroy(group);cache.Sync(scene,{},0);scene.Flush();
    Check(cache.Rows().size()==2&&!scene.Find(child),"Pending deletion invalidates cached pointers before destruction flush");
    cache.Sync(scene,{},0);Check(cache.Rows().size()==2,"Flush retains the already invalidated hierarchy");
}

void HistoryAndPlay(){
    ComponentRegistry registry;EditorSession editor(registry);HierarchyCache cache;
    auto first=editor.scene->CreateGameObject("First").GetID();auto second=editor.scene->CreateGameObject("Second").GetID();
    cache.Sync(*editor.scene,{},0);auto builds=cache.RebuildCount();
    editor.Edit([&](Scene& scene){scene.Find(second)->SetParent(scene.Find(first));});
    cache.Sync(*editor.scene,{},0);Check(cache.Rows().size()==1,"Edit changes the cached hierarchy");
    editor.Undo();cache.Sync(*editor.scene,{},0);Check(cache.Rows().size()==2,"In-place Undo invalidates changed relationships");
    editor.Redo();cache.Sync(*editor.scene,{},0);Check(cache.Rows().size()==1&&cache.RebuildCount()==builds+3,"Redo invalidates changed relationships exactly once");
    cache.SetExpanded(first,true);cache.Sync(*editor.scene,{},0);
    editor.Edit([&](Scene& scene){scene.Destroy(first);});cache.Sync(*editor.scene,{},0);Check(cache.Rows().empty(),"Deleted objects leave no dangling rows");
    editor.Undo();cache.Sync(*editor.scene,{},0);Check(cache.Rows().size()==1&&cache.Rows()[0].object==editor.scene->Find(first),"Undo recreation refreshes pointers for the same entity ID");
    editor.Play();cache.Sync(*editor.scene,{},0);Check(cache.Rows()[0].object==editor.scene->Find(first),"Play scene replacement rebuilds pointers");
    editor.scene->CreateGameObject("Runtime");cache.Sync(*editor.scene,{},0);Check(cache.Rows().size()==2,"Runtime creation updates the hierarchy");
    editor.Stop();cache.Sync(*editor.scene,{},0);Check(cache.Rows().size()==1&&cache.Rows()[0].object==editor.scene->Find(first),"Stop restores edit hierarchy and pointers");
}

struct HierarchyFixture {
    Workspace workspace;SceneInspector inspector;
    std::vector<EntityID> ids;
    explicit HierarchyFixture(const fs::path& root):workspace(root,true){
        workspace.editor.Replace(CreateScope<Scene>());
        for(int i=0;i<1000;++i)ids.push_back(workspace.editor.scene->CreateGameObject("Grass "+std::to_string(i)).GetID());
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    }
    ~HierarchyFixture(){ImGui::DestroyContext();}
    void Draw(ImVec2 mouse={-1,-1},bool left=false,bool right=false,float scroll=-1){
        auto& io=ImGui::GetIO();io.AddMousePosEvent(mouse.x,mouse.y);io.AddMouseButtonEvent(0,left);io.AddMouseButtonEvent(1,right);
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({300,220});
        ImGui::Begin("Cached hierarchy",nullptr,ImGuiWindowFlags_NoSavedSettings);
        if(scroll>=0)ImGui::SetScrollY(scroll);
        SceneInspectorTestAccess::Hierarchy(inspector,workspace);
        ImGui::End();ImGui::Render();
    }
    void Click(ImVec2 point,bool right=false){Draw(point);Draw(point,!right,right);Draw(point,!right,right);Draw(point);}
};

void ClippedUI(const fs::path& root){
    HierarchyFixture test(root);test.Draw();test.Draw();
    Check(SceneInspectorTestAccess::Rows(test.inspector)==1000,"All 1000 logical rows remain in the hierarchy");
    auto builds=SceneInspectorTestAccess::Rebuilds(test.inspector),flats=SceneInspectorTestAccess::Flattenings(test.inspector);
    for(int i=0;i<120;++i){test.Draw();Check(SceneInspectorTestAccess::Drawn(test.inspector)<20,"Only viewport rows are submitted to ImGui");}
    Check(SceneInspectorTestAccess::Rebuilds(test.inspector)==builds&&SceneInspectorTestAccess::Flattenings(test.inspector)==flats,"Idle clipped rendering never rebuilds or flattens the hierarchy");
    test.Click({75,34});Check(test.workspace.editor.selected==test.ids.front(),"A clipped row retains normal entity selection");
    auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Shift,true);test.Click({75,51});io.AddKeyEvent(ImGuiMod_Shift,false);test.Draw();
    Check(test.workspace.editor.selection.size()==2,"Shift selection works with stable row IDs");
    test.Click({75,34},true);Check(SceneInspectorTestAccess::Popup(test.inspector)==test.ids.front(),"Right click opens the entity action popup");
    test.Draw({75,34},false,false,9000);test.Draw();
    Check(SceneInspectorTestAccess::Popup(test.inspector)==test.ids.front()&&SceneInspectorTestAccess::Drawn(test.inspector)<22,"An active context popup survives scrolling its row out of view");
    io.AddKeyEvent(ImGuiKey_Escape,true);test.Draw();io.AddKeyEvent(ImGuiKey_Escape,false);test.Draw();
    test.Draw({-1,-1},false,false,9000);test.Draw();
    Check(SceneInspectorTestAccess::Rebuilds(test.inspector)==builds&&SceneInspectorTestAccess::Flattenings(test.inspector)==flats,"Scrolling changes only clipping, not the hierarchy cache");
    Check(SceneInspectorTestAccess::Drawn(test.inspector)<20,"Scrolling does not submit offscreen rows");
}

int main(int argc,char** argv){try{
    Check(argc==2,"Provide the test project directory");CacheLifecycle();HistoryAndPlay();
    ClippedUI(fs::path(argv[1])/NewAssetID());
    std::cout<<"Hierarchy revisions, cached adjacency, clipped rendering and selection passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
