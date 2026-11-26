#pragma once
#include "Rice/Benchmark.h"
#include "Project/Workspace.h"
#include "Project/Installation.h"
#include "EditorEditUI.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "Renderer/ViewportTarget.h"
#include "TransformGizmo.h"
#include "EditorClipboard.h"
#include "EditorGroups.h"
#include "HierarchyCache.h"
#include "EditorCamera.h"
#include "ShapeEditor.h"
#include "PivotAnchorUI.h"
#include "BenchmarkPanel.h"
#include "StreamingPanel.h"
#include "ScriptPanel.h"
#include "Platform/FileDialog.h"
#include <fstream>
#include <iomanip>
namespace Rice {
inline bool EditString(const char* label,std::string& value,const BeforeEditorEdit& beforeEdit={}){ RICE_BENCHMARK_FUNCTION(); std::vector<char> buffer(std::max<size_t>(4096,value.size()+256));std::copy(value.begin(),value.end(),buffer.begin());if(ImGui::InputText(label,buffer.data(),buffer.size())){if(beforeEdit)beforeEdit();value=buffer.data();return true;}return false;}
inline void DrawProperty(const char* label,Property p,const BeforeEditorEdit& beforeEdit={}){ RICE_BENCHMARK_FUNCTION(); std::visit([&](auto* value){ RICE_BENCHMARK_FUNCTION(); using T=std::remove_cvref_t<decltype(*value)>;
    if constexpr(std::is_same_v<T,bool>)EditValue(*value,beforeEdit,[&](bool& next){return ImGui::Checkbox(label,&next);});
    else if constexpr(std::is_same_v<T,int>)EditValue(*value,beforeEdit,[&](int& next){return ImGui::DragInt(label,&next,1,0,10000);});
    else if constexpr(std::is_same_v<T,float>)EditValue(*value,beforeEdit,[&](float& next){return ImGui::DragFloat(label,&next,.02f);});
    else if constexpr(std::is_same_v<T,std::string>)EditString(label,*value,beforeEdit);
    else if constexpr(std::is_same_v<T,Vector2>)EditValue(*value,beforeEdit,[&](Vector2& next){return ImGui::DragFloat2(label,&next.x,.1f);});
    else if constexpr(std::is_same_v<T,Vector3>)EditValue(*value,beforeEdit,[&](Vector3& next){return ImGui::DragFloat3(label,&next.x,.05f);});
    else if constexpr(std::is_same_v<T,Vector4>)EditValue(*value,beforeEdit,[&](Vector4& next){return ImGui::ColorEdit4(label,&next.x);});
},p);}
class GameUI {
public:
    static void DrawContents(Workspace& w,ImVec2 origin){
        RICE_BENCHMARK_FUNCTION();
        for(auto& obj:w.editor.scene->GetObjects())if(obj->IsActive()){
            ImGui::PushID(obj.get());
            if(auto* t=obj->GetComponent<UIText>();t&&t->enabled){auto text=t->text;auto marker=text.find("{score}");if(marker!=std::string::npos)text.replace(marker,7,std::to_string(w.score));ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),glm::clamp(t->size,1.f,200.f),{origin.x+t->position.x,origin.y+t->position.y},ImGui::ColorConvertFloat4ToU32({t->color.x,t->color.y,t->color.z,t->color.w}),text.c_str());}
            if(auto* b=obj->GetComponent<UIButton>();b&&b->enabled){ImGui::SetCursorScreenPos({origin.x+b->position.x,origin.y+b->position.y});b->clicked=ImGui::Button(b->text.c_str(),{std::max(1.f,b->size.x),std::max(1.f,b->size.y)});if(b->clicked&&w.editor.playing&&!w.editor.paused){if(b->onClick)b->onClick();auto it=w.actions.find(b->action);if(it!=w.actions.end())it->second();}}
            ImGui::PopID();
        }
    }
    static void Draw(Workspace& w){
        RICE_BENCHMARK_FUNCTION();
        auto* viewport=ImGui::GetMainViewport();ImGui::SetNextWindowPos(viewport->Pos);ImGui::SetNextWindowSize(viewport->Size);
        ImGui::Begin("Game Canvas",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking);
        DrawContents(w,viewport->Pos);ImGui::End();
    }
};
class SceneInspector {
public:
    enum class CloseResult {None,CloseProject,Exit};
private:
    friend struct SceneInspectorTestAccess;
    enum class ProjectAction {Close,Exit,Open,New};
    struct PendingProject {ProjectAction action;fs::path path;};
    std::optional<PendingProject> pendingProject;
    bool confirmingClose=false;
    std::string closeError;
    std::string newProjectName,projectLocation,renamePath,exportPath;
    fs::path installRoot,exportProject;
    bool requestNewProject=false;
    EditorSession::EditSnapshot interactionSnapshot;
    EntityID interactionObject=0;
    uint64_t interactionScene=0;
    ImGuiID interactionItem=0;
    bool interactionSettings=false;
    std::string sceneName="New Scene",prefabName,selectedPrefab;
    bool duplicateScene=false;
    EntityID prefabRoot=0;
    Vector3 prefabPosition{0};
    int prefabWarmCount=10;
    bool editing=false,layoutInitialized=false,resetLayout=false;
    bool hierarchyOpen=true,inspectorOpen=true,projectOpen=true,consoleOpen=true,inputOpen=true,sceneOpen=true,gameOpen=true;
    bool gameFocused=false;
    bool benchmarkOpen=false;
    BenchmarkPanel benchmarkPanel;
    StreamingPanel streamingPanel;
    ScriptPanel scriptPanel;
    bool autoScrollLogs=true;
    uint64_t lastLogSequence=0;
    std::string layoutPath,scriptEditor,editorSettingsPath;
    std::string projectOpenError;
    ViewportTarget sceneTarget,gameTarget;
    TransformGizmo gizmo;
    ShapeEditor shapeEditor;
    MaterialOverwritePrompt materialPrompt;
    uint64_t materialHistoryRevision=0;
    EditorClipboard clipboard;
    HierarchyCache hierarchy;
    EntityID hierarchyPopupObject=0,hierarchyDragObject=0;
    size_t hierarchyRowsDrawn=0;
    float hierarchyStartY=0,hierarchyRowHeight=0;
    bool sceneDrawn=false,moveView=false,moveObject=true;
    CameraWheelGesture wheelGesture;EntityID orbitTarget=0;Vector3 orbitPivot{0};
    EditorCamera editorCamera;CameraPointerGesture cameraGesture;int cameraClick=-1;
    std::function<void()> pendingObjectAction;
    void FinishEdit(Workspace& w){
        RICE_BENCHMARK_FUNCTION();
        if(!editing)return;
        editing=false;
        if(!w.editor.playing&&interactionScene==w.editor.scene->GetInstanceID()){
            w.editor.scene->Flush();
            w.editor.Record(interactionSnapshot);
        }
        interactionSnapshot={};interactionObject=0;interactionScene=0;interactionItem=0;interactionSettings=false;
    }
    void BeginObjectEdit(Workspace& w,EntityID id){
        RICE_BENCHMARK_FUNCTION();
        if(w.editor.playing)return;
        auto item=ImGui::GetCurrentContext()->ActiveId;
        if(editing&&(interactionSettings||interactionObject!=id||interactionScene!=w.editor.scene->GetInstanceID()||interactionItem!=item))FinishEdit(w);
        if(editing)return;
        interactionSnapshot=w.editor.CaptureObject(id);interactionObject=id;interactionScene=w.editor.scene->GetInstanceID();interactionItem=item;interactionSettings=false;editing=true;
    }
    void BeginSettingsEdit(Workspace& w){
        RICE_BENCHMARK_FUNCTION();
        if(w.editor.playing)return;
        auto item=ImGui::GetCurrentContext()->ActiveId;
        if(editing&&(!interactionSettings||interactionScene!=w.editor.scene->GetInstanceID()||interactionItem!=item))FinishEdit(w);
        if(editing)return;
        interactionSnapshot=w.editor.CaptureSettings();interactionObject=0;interactionScene=w.editor.scene->GetInstanceID();interactionItem=item;interactionSettings=true;editing=true;
    }
    void EndFrameEdits(Workspace& w){
        RICE_BENCHMARK_FUNCTION();
        w.editor.scene->Flush();
        if(editing&&(!ImGui::IsAnyItemActive()||ImGui::GetCurrentContext()->ActiveId!=interactionItem))FinishEdit(w);
    }
    void DrawObjectProperty(Workspace& w,GameObject& object,const char* label,Property property){
        RICE_BENCHMARK_FUNCTION();
        DrawProperty(label,property,[&]{BeginObjectEdit(w,object.GetID());});
    }
    void SyncMaterialPrompt(Workspace& w){
        RICE_BENCHMARK_FUNCTION();
        // Undo/Redo now preserve the Scene instance. A proposal still belongs
        // to the state in which it was opened, rather than the restored state.
        if(materialHistoryRevision!=w.editor.historyRevision){
            materialPrompt.Return();materialHistoryRevision=w.editor.historyRevision;
        }
    }
    static std::string SceneLabel(const fs::path& path){
        RICE_BENCHMARK_FUNCTION();
        auto utf8=path.generic_u8string();
        return {utf8.begin(),utf8.end()};
    }
    static fs::path AvailableBuildFile(fs::path requested){
        auto stem=requested.stem().wstring();
        auto candidate=requested;
        std::error_code error;
        for(unsigned copy=2;fs::exists(candidate,error)&&!error;++copy){
            candidate=requested.parent_path()/(stem+L" ("+std::to_wstring(copy)+L").exe");
        }
        return candidate;
    }
    void ResetSceneInteraction(Workspace& w){
        RICE_BENCHMARK_FUNCTION();
        FinishEdit(w);
        materialPrompt.Return();
        shapeEditor.Close(w.editor);
        gizmo.Cancel(w.editor);
        cameraGesture.Cancel();
        wheelGesture.Cancel();
        interactionSnapshot={};
        contextObject=0;
        orbitTarget=0;
    }
    void DrawSceneSettings(Workspace& w){
        auto& settings=w.editor.scene->settings;
        BeforeEditorEdit beforeEdit=[&]{BeginSettingsEdit(w);};
        ImGui::BeginDisabled(w.editor.playing);
        ImGui::PushItemWidth(ImGui::GetFontSize()*14.f);
        EditValue(settings.skybox,beforeEdit,[](bool& next){return ImGui::Checkbox("Skybox",&next);});
        ImGui::BeginDisabled(!settings.skybox);AssetPicker(w,"Skybox asset",settings.skyboxAsset,beforeEdit);ImGui::EndDisabled();
        EditValue(settings.directionalLight,beforeEdit,[](bool& next){return ImGui::Checkbox("Default directional light",&next);});
        ImGui::BeginDisabled(!settings.directionalLight);
        EditValue(settings.lightColor,beforeEdit,[](Vector4& next){return ImGui::ColorEdit4("Light color",&next.x);});
        EditValue(settings.lightIntensity,beforeEdit,[](float& next){return ImGui::DragFloat("Light intensity",&next,.02f,0,10000,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
        EditValue(settings.lightDirection,beforeEdit,[](Vector3& next){return ImGui::DragFloat3("Light direction",&next.x,.05f);});
        ImGui::EndDisabled();
        EditValue(settings.ground,beforeEdit,[](bool& next){return ImGui::Checkbox("Ground (Y = 0)",&next);});
        ImGui::BeginDisabled(!settings.ground);EditValue(settings.groundCollision,beforeEdit,[](bool& next){return ImGui::Checkbox("Ground collision",&next);});ImGui::EndDisabled();
        ImGui::TextUnformatted("Editor guides (not exported)");
        auto visibility=[&](const char* label,float& value){bool on=value!=0;if(ImGui::Checkbox(label,&on)){beforeEdit();value=on?1.f:0.f;}};
        visibility("X axis",settings.axes.x);ImGui::SameLine();visibility("Y axis",settings.axes.y);ImGui::SameLine();visibility("Z axis",settings.axes.z);
        visibility("XY grid",settings.grids.x);ImGui::SameLine();visibility("YZ grid",settings.grids.y);ImGui::SameLine();visibility("ZX grid",settings.grids.z);
        ImGui::PopItemWidth();
        ImGui::EndDisabled();
    }
    void SceneControls(Workspace& w){
        RICE_BENCHMARK_FUNCTION();
        ImGui::BeginDisabled(w.editor.playing);
        auto current=SceneLabel(w.project->ActiveScene());
        if(ImGui::BeginCombo("Scene",current.c_str())){
            for(const auto& scene:w.project->SceneFiles()){
                auto label=SceneLabel(scene);
                if(ImGui::Selectable(label.c_str(),scene==w.project->ActiveScene())){
                    pendingObjectAction=[this,&w,scene]{
                        ResetSceneInteraction(w);
                        w.SwitchScene(scene);
                    };
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Switch scenes. The current scene is saved first.");
        if(ImGui::Button("New Scene")){
            duplicateScene=false;sceneName="New Scene";ImGui::OpenPopup("Create Scene");
        }
        ImGui::SameLine();
        if(ImGui::Button("Duplicate Scene")){
            duplicateScene=true;sceneName=SceneLabel(w.project->ActiveScene().stem())+" Copy";
            ImGui::OpenPopup("Create Scene");
        }
        if(ImGui::BeginPopup("Create Scene")){
            EditString("Name",sceneName);
            if(ImGui::Button("Create")){
                auto name=sceneName;
                auto duplicate=duplicateScene;
                pendingObjectAction=[this,&w,name,duplicate]{
                    ResetSceneInteraction(w);
                    w.NewScene(name,duplicate);
                };
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        auto* selected=w.editor.scene->Find(w.editor.selected);
        ImGui::BeginDisabled(!selected||selected->IsPoolAvailable());
        if(ImGui::Button("Save as Prefab")){
            prefabRoot=selected->GetID();prefabName=selected->name;
            ImGui::OpenPopup("Save Prefab");
        }
        ImGui::EndDisabled();
        if(ImGui::BeginPopup("Save Prefab")){
            EditString("Prefab name",prefabName);
            auto names=w.prefabs.Names();
            bool exists=std::find(names.begin(),names.end(),prefabName)!=names.end();
            if(ImGui::Button(exists?"Update Prefab":"Save Prefab")){
                auto root=prefabRoot;
                auto name=prefabName;
                pendingObjectAction=[this,&w,root,name]{w.SavePrefab(root,name);selectedPrefab=name;};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();
    }
    void PrefabControls(Workspace& w){
        RICE_BENCHMARK_FUNCTION();
        if(!ImGui::CollapsingHeader("Prefabs",ImGuiTreeNodeFlags_DefaultOpen))return;
        auto names=w.prefabs.Names();
        if(std::find(names.begin(),names.end(),selectedPrefab)==names.end())
            selectedPrefab=names.empty()?std::string{}:names.front();
        if(ImGui::BeginCombo("Prefab",selectedPrefab.empty()?"Select a prefab":selectedPrefab.c_str())){
            for(const auto& name:names)if(ImGui::Selectable(name.c_str(),name==selectedPrefab))selectedPrefab=name;
            ImGui::EndCombo();
        }
        ImGui::DragFloat3("Spawn position",&prefabPosition.x,.1f);
        ImGui::BeginDisabled(selectedPrefab.empty());
        if(ImGui::Button("Instantiate")){
            Guard(w,[&]{
                FinishEdit(w);
                EntityID id=0;
                auto spawn=[&](Scene&){id=w.InstantiatePrefab(selectedPrefab,prefabPosition).GetID();};
                if(w.editor.playing)spawn(*w.editor.scene);
                else w.editor.Edit(spawn);
                w.editor.Select(id);
            });
        }
        ImGui::BeginDisabled(!w.editor.playing);
        ImGui::SameLine();
        if(ImGui::Button("Prewarm"))Guard(w,[&]{w.PrewarmPrefab(selectedPrefab,size_t(prefabWarmCount));});
        ImGui::SetNextItemWidth(100);
        ImGui::DragInt("Pool capacity",&prefabWarmCount,1,0,10000,"%d",ImGuiSliderFlags_AlwaysClamp);
        prefabWarmCount=std::clamp(prefabWarmCount,0,10000);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        auto* selected=w.editor.scene->Find(w.editor.selected);
        auto root=w.editor.playing&&selected?w.prefabPool.Root(*w.editor.scene,selected->GetID()):0;
        ImGui::BeginDisabled(!root||selected->IsPoolAvailable());
        if(ImGui::Button("Release selected to pool"))Guard(w,[&]{
            if(w.ReleasePrefab(root))w.editor.Select(0);
        });
        ImGui::EndDisabled();
        ImGui::TextWrapped("Select an object in Hierarchy to save it as a prefab. Play uses pooled instances.");
    }
    void DefaultLayout(ImGuiID root){
        RICE_BENCHMARK_FUNCTION();
        ImGui::DockBuilderRemoveNode(root);ImGui::DockBuilderAddNode(root,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(root,ImGui::GetMainViewport()->WorkSize);
        ImGuiID center=root,left,right,bottom;
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.18f,&left,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.28f,&right,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.28f,&bottom,&center);
        ImGui::DockBuilderDockWindow("Hierarchy",left);ImGui::DockBuilderDockWindow("Inspector",right);
        ImGui::DockBuilderDockWindow("Scene",center);ImGui::DockBuilderDockWindow("Game",center);
        ImGui::DockBuilderDockWindow("Project",bottom);ImGui::DockBuilderDockWindow("Console",bottom);ImGui::DockBuilderDockWindow("Input",bottom);
        ImGui::DockBuilderDockWindow("Benchmark",bottom);
        ImGui::DockBuilderFinish(root);
    }
    bool NavigateCamera(bool hovered,ImVec2 size,bool excluded){
        RICE_BENCHMARK_FUNCTION();
        auto& io=ImGui::GetIO();bool focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        bool blocked=!moveView||!focused||excluded||io.WantTextInput||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)||ImGui::IsKeyPressed(ImGuiKey_Escape);
        int pressed=ImGui::IsMouseClicked(1)?1:ImGui::IsMouseClicked(0)?0:-1;
        auto input=cameraGesture.Update({io.MousePos.x,io.MousePos.y},pressed,ImGui::IsMouseDown(0),ImGui::IsMouseDown(1),hovered,blocked);
        cameraClick=input.click;bool changed=false;
        if(glm::length(input.delta)>0){if(input.button==1)editorCamera.Look(input.delta);else editorCamera.Pan(input.delta,size.y);changed=true;}
        if(!blocked&&!io.KeyCtrl&&!io.KeyAlt&&!io.KeySuper){
            Vector3 keyboardInput(float(ImGui::IsKeyDown(ImGuiKey_D))-float(ImGui::IsKeyDown(ImGuiKey_A)),float(ImGui::IsKeyDown(ImGuiKey_E))-float(ImGui::IsKeyDown(ImGuiKey_Q)),float(ImGui::IsKeyDown(ImGuiKey_W))-float(ImGui::IsKeyDown(ImGuiKey_S)));
            if(glm::length(keyboardInput)>0){editorCamera.Move(keyboardInput,io.DeltaTime);changed=true;}
        }return changed;
    }
    bool DragSceneCameraOrbit(const Scene& scene,const CameraWheelGesture::Result& wheel){
        RICE_BENCHMARK_FUNCTION();
        if(!wheel.captured||wheel.orbitDelta==Vector2(0))return false;
        editorCamera.Orbit(scene.Find(orbitTarget)?orbitPivot:Vector3(0),wheel.orbitDelta);
        return true;
    }
    void View(Workspace& w,const char* name,bool& open,ViewportTarget& target,bool game,const std::function<Matrix4(int,int,bool)>& render){
        RICE_BENCHMARK_FUNCTION();
        if(!open)return;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
        if(ImGui::Begin(name,&open,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)){
            auto size=ImGui::GetContentRegionAvail();auto origin=ImGui::GetCursorScreenPos();
            if(size.x>=1&&size.y>=1){
                int width=static_cast<int>(size.x),height=static_cast<int>(size.y);
                Matrix4 viewProjection(1);
                auto texture=target.Render(width,height,[&]{ RICE_BENCHMARK_FUNCTION(); viewProjection=render(width,height,game);});
                if(game)ImGui::Image(static_cast<ImTextureID>(texture),size,{0,1},{1,0});
                else {
                    ImGui::InvisibleButton("SceneCanvas",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight|ImGuiButtonFlags_MouseButtonMiddle);
                    ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(texture),origin,{origin.x+size.x,origin.y+size.y},{0,1},{1,0});
                    if(gizmo.Active()&&!w.editor.scene->Find(gizmo.DraggingObject()))gizmo.Cancel(w.editor);
                    bool canvasHovered=ImGui::IsItemHovered();
                    auto& io=ImGui::GetIO();
                    // Close a field interaction before the viewport starts a
                    // separate transform transaction in the same UI frame.
                    if(editing&&((canvasHovered&&(ImGui::IsMouseClicked(0)||ImGui::IsMouseClicked(1)||ImGui::IsMouseClicked(2)))||ImGui::GetCurrentContext()->ActiveId!=interactionItem))FinishEdit(w);
                    bool blocked=io.WantTextInput||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
                    auto wheel=wheelGesture.Update({io.MousePos.x,io.MousePos.y},ImGui::IsMouseClicked(2),ImGui::IsMouseDown(2),canvasHovered&&!blocked,
                        blocked||ImGui::IsKeyPressed(ImGuiKey_Escape)||!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
                    ImVec2 crosshair{origin.x+size.x*.5f,origin.y+size.y*.5f};
                    if(wheel.captured&&ImGui::IsMouseClicked(2)){
                        orbitTarget=GizmoMath::Pick(*w.editor.scene,GizmoMath::ScreenRay({crosshair.x,crosshair.y},{origin.x,origin.y},{size.x,size.y},viewProjection),false,true);
                        if(auto* obj=w.editor.scene->Find(orbitTarget)){auto [low,high]=GizmoMath::PrimitiveBounds(*obj);orbitPivot=Vector3(obj->GetWorldMatrix()*Vector4((low+high)*.5f,1));}
                    }
                    float wheelZoom=canvasHovered&&!blocked&&!wheel.captured?io.MouseWheel:0;
                    if(DragSceneCameraOrbit(*w.editor.scene,wheel))target.Render(width,height,[&]{ RICE_BENCHMARK_FUNCTION(); viewProjection=render(width,height,game);});
                    bool wheelCaptured=wheel.captured||wheelZoom!=0;
                    if(wheelCaptured){gizmo.Cancel(w.editor);cameraGesture.Cancel();}
                    if(wheel.toggle){moveView=!moveView;moveObject=!moveView;}
                    if(wheelZoom!=0){editorCamera.Zoom(wheelZoom);target.Render(width,height,[&]{ RICE_BENCHMARK_FUNCTION(); viewProjection=render(width,height,game);});}

                    if(!moveObject)gizmo.Cancel(w.editor);
                    cameraClick=-1;
                    bool cameraWasActive=cameraGesture.Active();
                    if(cameraWasActive&&NavigateCamera(canvasHovered,size,wheelCaptured))target.Render(width,height,[&]{ RICE_BENCHMARK_FUNCTION(); viewProjection=render(width,height,game);});
                    auto previewBefore=w.editor.previewRevision;
                    gizmo.BeginFrame();
                    auto selected=w.editor.selection;
                    if(selected.empty()&&w.editor.selected)selected.insert(w.editor.selected);
                    bool anchorHovered=false;
                    for(auto id:selected)if(auto* object=w.editor.scene->Find(id))anchorHovered|=DrawSceneAnchor(*object,origin,size,viewProjection,canvasHovered&&!wheelCaptured&&!cameraWasActive);
                    if(moveObject)for(auto id:selected)gizmo.Draw(w.editor,origin,size,viewProjection,id,canvasHovered&&!wheelCaptured&&!cameraWasActive&&!anchorHovered);
                    if(!cameraWasActive&&NavigateCamera(canvasHovered,size,wheelCaptured||(moveObject&&(gizmo.Active()||gizmo.consumed||gizmo.handleHovered))))target.Render(width,height,[&]{ RICE_BENCHMARK_FUNCTION(); viewProjection=render(width,height,game);});
                    // Render preview changes immediately, without running gameplay Update/physics.
                    if(w.editor.previewRevision!=previewBefore)target.Render(width,height,[&]{ RICE_BENCHMARK_FUNCTION(); render(width,height,game);});
                    bool hovered=canvasHovered;
                    auto mouse=ImGui::GetIO().MousePos;
                    if(moveObject&&!wheelCaptured&&hovered&&!gizmo.Active()&&!gizmo.consumed&&!anchorHovered){
                        auto hit=GizmoMath::Pick(*w.editor.scene,GizmoMath::ScreenRay({mouse.x,mouse.y},{origin.x,origin.y},{size.x,size.y},viewProjection));
                        if(moveView?cameraClick==0:ImGui::IsMouseClicked(0))w.editor.Select(hit,ImGui::GetIO().KeyShift);
                        if(hit&&(moveView?cameraClick==1:ImGui::IsMouseClicked(1))){if(!w.editor.IsSelected(hit))w.editor.Select(hit);contextObject=hit;ImGui::OpenPopup("View object actions");}
                    }
                    if(wheel.captured&&ImGui::IsMouseDown(2)){
                        auto* draw=ImGui::GetWindowDrawList();ImU32 color=w.editor.scene->Find(orbitTarget)?IM_COL32(255,195,45,255):IM_COL32(220,220,220,255);
                        ImGui::PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
                        for(float thickness:{4.f,2.f}){auto ink=thickness>2?IM_COL32(0,0,0,230):color;
                            draw->AddLine({crosshair.x-10,crosshair.y},{crosshair.x+10,crosshair.y},ink,thickness);
                            draw->AddLine({crosshair.x,crosshair.y-10},{crosshair.x,crosshair.y+10},ink,thickness);}
                        ImGui::PopClipRect();
                    }
                    if(!wheel.captured)orbitTarget=0;
                    if(moveObject)ObjectMenu(w,"View object actions",contextObject);
                    sceneDrawn=true;
                }
                if(game){gameFocused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);ImGui::PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);GameUI::DrawContents(w,origin);ImGui::PopClipRect();}
            }
        }
        ImGui::End();ImGui::PopStyleVar();
    }
    EntityID contextObject=0;
    std::string newAction="Action"; int newKey=69;
    template<class F>void Guard(Workspace& w,F fn){ RICE_BENCHMARK_FUNCTION(); try{fn();}catch(const std::exception& e){w.Log(e.what(),spdlog::level::err);}}
    void Shortcuts(Workspace& w){
        RICE_BENCHMARK_FUNCTION();
        auto& io=ImGui::GetIO();
        if(w.editor.playing||scriptPanel.Focused()||shapeEditor.Focused()||shapeEditor.Editing()||io.WantTextInput||ImGui::IsAnyItemActive()||gizmo.Active()||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel))return;
        auto pressed=[](ImGuiKey key){ RICE_BENCHMARK_FUNCTION(); return ImGui::IsKeyPressed(key,false);};
        bool command=io.KeyCtrl&&(pressed(ImGuiKey_Z)||pressed(ImGuiKey_Y)||pressed(ImGuiKey_X)||pressed(ImGuiKey_C)||pressed(ImGuiKey_V)||pressed(ImGuiKey_T)||pressed(ImGuiKey_G));
        bool remove=!io.KeyCtrl&&!io.KeyAlt&&!io.KeySuper&&(pressed(ImGuiKey_Delete)||pressed(ImGuiKey_Backspace));
        if(!command&&!remove)return;
        FinishEdit(w);
        Guard(w,[&]{
            RICE_BENCHMARK_FUNCTION();
            if(command){
                if(pressed(ImGuiKey_G)){if(io.KeyShift)EditorGroups::Ungroup(w.editor,w.editor.selected);else EditorGroups::Create(w.editor,w.editor.selection);}
                else if(pressed(ImGuiKey_T)&&!io.KeyShift)EditorGroups::ControlChildren(w.editor,w.editor.selected);
                else if(pressed(ImGuiKey_Z))w.editor.Undo();
                else if(pressed(ImGuiKey_Y))w.editor.Redo();
                else if(pressed(ImGuiKey_C))clipboard.CopyObject(*w.editor.scene,w.editor.selected,w.registry);
                else if(pressed(ImGuiKey_X))clipboard.CutObject(w.editor,w.registry);
                else if(pressed(ImGuiKey_V))clipboard.PasteObject(w.editor,w.registry);
            }else w.editor.DeleteSelection();
        });
    }
    void ObjectMenu(Workspace& w,const char* popup,EntityID id){
        RICE_BENCHMARK_FUNCTION();
        if(!ImGui::BeginPopup(popup))return;
        ImGui::BeginDisabled(w.editor.playing);
        if(ImGui::MenuItem("Select All Children"))pendingObjectAction=[&,id]{ RICE_BENCHMARK_FUNCTION(); w.editor.SelectChildren(id);};
        if(ImGui::MenuItem("Copy Object"))pendingObjectAction=[&,id]{ RICE_BENCHMARK_FUNCTION(); clipboard.CopyObject(*w.editor.scene,id,w.registry);};
        if(ImGui::MenuItem("Copy All Components"))pendingObjectAction=[&,id]{ RICE_BENCHMARK_FUNCTION(); if(auto* obj=w.editor.scene->Find(id))clipboard.CopyAllComponents(*obj,w.registry);};
        if(ImGui::MenuItem("Paste All Components",nullptr,false,clipboard.HasComponents()))pendingObjectAction=[&,id]{ RICE_BENCHMARK_FUNCTION(); w.editor.Edit([&](Scene& scene){ RICE_BENCHMARK_FUNCTION(); if(auto* obj=scene.Find(id))clipboard.PasteComponents(*obj,w.registry);});};
        if(ImGui::MenuItem("Clone"))pendingObjectAction=[&,id]{ RICE_BENCHMARK_FUNCTION(); EditorClipboard copy;copy.CopyObject(*w.editor.scene,id,w.registry);copy.PasteObject(w.editor,w.registry);};
        if(ImGui::MenuItem("Reproduce"))pendingObjectAction=[&,id]{ RICE_BENCHMARK_FUNCTION(); w.editor.Edit([&](Scene& scene){ RICE_BENCHMARK_FUNCTION(); if(auto* parent=scene.Find(id)){auto& child=scene.CreateGameObject("Child");child.SetParent(parent);w.editor.Select(child.GetID());}});};
        if(ImGui::MenuItem("Group","Ctrl+G"))pendingObjectAction=[&]{ RICE_BENCHMARK_FUNCTION(); EditorGroups::Create(w.editor,w.editor.selection);};
        if(auto* obj=w.editor.scene->Find(id);obj&&obj->isGroup)if(ImGui::MenuItem("Ungroup","Ctrl+Shift+G"))pendingObjectAction=[&,id]{ RICE_BENCHMARK_FUNCTION(); EditorGroups::Ungroup(w.editor,id);};
        if(ImGui::MenuItem("Control Children","Ctrl+T"))pendingObjectAction=[&,id]{ RICE_BENCHMARK_FUNCTION(); EditorGroups::ControlChildren(w.editor,id);};
        ImGui::EndDisabled();ImGui::EndPopup();
    }
    void HierarchyNode(Workspace& w,GameObject& obj){
        RICE_BENCHMARK_FUNCTION();
        ++hierarchyRowsDrawn;
        ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(obj.GetID())));
        bool children=hierarchy.HasChildren(obj.GetID());
        auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if(!children)flags|=ImGuiTreeNodeFlags_Leaf;
        if(w.editor.IsSelected(obj.GetID()))flags|=ImGuiTreeNodeFlags_Selected;
        ImGui::SetNextItemOpen(!children||hierarchy.Expanded(obj.GetID()),ImGuiCond_Always);
        bool open=ImGui::TreeNodeEx("Object",flags,"%s%s",obj.isGroup?"[Group] ":"",obj.name.c_str());
        if(children&&ImGui::IsItemToggledOpen())hierarchy.SetExpanded(obj.GetID(),open);
        if(ImGui::IsItemClicked(0)&&!ImGui::IsItemToggledOpen())w.editor.Select(obj.GetID(),ImGui::GetIO().KeyShift);
        if(ImGui::IsItemClicked(1)){if(!w.editor.IsSelected(obj.GetID()))w.editor.Select(obj.GetID());hierarchyPopupObject=obj.GetID();ImGui::OpenPopup("Object actions");}
        auto rowMin=ImGui::GetItemRectMin(),rowMax=ImGui::GetItemRectMax();bool insertionAfter=false;
        auto insertionLine=[&](float y){ RICE_BENCHMARK_FUNCTION(); auto* draw=ImGui::GetWindowDrawList();auto color=IM_COL32(255,220,40,255);draw->AddLine({rowMin.x,y},{rowMax.x,y},color,2.f);draw->AddTriangleFilled({rowMin.x,y-4},{rowMin.x+6,y},{rowMin.x,y+4},color);};
        if(!w.editor.playing&&ImGui::BeginDragDropSource()){auto id=obj.GetID();hierarchyDragObject=id;ImGui::SetDragDropPayload("RICE_OBJECT",&id,sizeof(id));ImGui::TextUnformatted(obj.name.c_str());ImGui::EndDragDropSource();}
        if(!w.editor.playing&&ImGui::BeginDragDropTarget()){
            if(auto* payload=ImGui::AcceptDragDropPayload("RICE_OBJECT",ImGuiDragDropFlags_AcceptBeforeDelivery|ImGuiDragDropFlags_AcceptNoDrawDefaultRect)){
                auto source=*static_cast<const EntityID*>(payload->Data);auto target=obj.GetID();
                float zone=std::min(6.f,(rowMax.y-rowMin.y)*.25f),y=ImGui::GetIO().MousePos.y;
                int placement=y<rowMin.y+zone?-1:y>rowMax.y-zone?1:0;
                auto* child=w.editor.scene->Find(source);auto* parent=placement?obj.GetHierarchyParent():&obj;
                bool valid=child&&source!=target&&(!parent||!parent->DependsOn(child));
                if(valid){
                    if(placement<0)insertionLine(rowMin.y);
                    else if(placement>0)insertionAfter=true;
                    else ImGui::GetWindowDrawList()->AddRect(rowMin,rowMax,IM_COL32(100,180,255,255),0,0,2.f);
                    if(payload->IsDelivery())pendingObjectAction=[&,source,target,placement]{
                        RICE_BENCHMARK_FUNCTION();
                        if(placement)EditorGroups::InsertSibling(w.editor,source,target,placement>0);
                        else w.editor.Edit([&](Scene& scene){ RICE_BENCHMARK_FUNCTION(); if(auto* dragged=scene.Find(source))EditorGroups::Reparent(*dragged,scene.Find(target));});
                    };
                }
            }
            ImGui::EndDragDropTarget();
        }
        ObjectMenu(w,"Object actions",obj.GetID());
        if(hierarchyPopupObject==obj.GetID()&&!ImGui::IsPopupOpen("Object actions"))hierarchyPopupObject=0;
        if(insertionAfter){
            float y=rowMax.y;
            if(open&&children)if(auto index=hierarchy.Position(obj.GetID()))y=hierarchyStartY+hierarchy.Rows()[*index].subtreeEnd*hierarchyRowHeight-ImGui::GetStyle().ItemSpacing.y*.5f;
            insertionLine(y);
        }
        ImGui::PopID();
    }
    void DrawHierarchy(Workspace& w){
        RICE_BENCHMARK_FUNCTION();
        hierarchyRowsDrawn=0;
        hierarchy.Sync(*w.editor.scene,w.editor.selection,w.editor.selected);
        if(!ImGui::GetDragDropPayload())hierarchyDragObject=0;
        const auto& rows=hierarchy.Rows();
        hierarchyStartY=ImGui::GetCursorScreenPos().y;hierarchyRowHeight=ImGui::GetTextLineHeightWithSpacing();
        ImGuiListClipper clipper;clipper.Begin(static_cast<int>(rows.size()),hierarchyRowHeight);
        // Keep an active popup/drag source submitted even after scrolling it out
        // of view; all other off-screen rows require no ImGui work.
        for(auto id:{hierarchyPopupObject,hierarchyDragObject})if(auto index=hierarchy.Position(id))clipper.IncludeItemByIndex(static_cast<int>(*index));
        while(clipper.Step())for(int index=clipper.DisplayStart;index<clipper.DisplayEnd;++index){
            const auto& row=rows[size_t(index)];float indent=row.depth*ImGui::GetStyle().IndentSpacing;
            if(indent)ImGui::Indent(indent);
            HierarchyNode(w,*row.object);
            if(indent)ImGui::Unindent(indent);
        }
    }
    void ComponentMenu(Workspace& w,GameObject& obj,const std::string& name){
        RICE_BENCHMARK_FUNCTION();
        if(ImGui::BeginPopupContextItem("Component actions")){
            ImGui::BeginDisabled(w.editor.playing);
            if(ImGui::MenuItem("Copy component"))Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); clipboard.CopyComponent(obj,name,w.registry);});
            if(ImGui::MenuItem("Copy all components"))Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); clipboard.CopyAllComponents(obj,w.registry);});
            if(ImGui::MenuItem("Paste component(s)",nullptr,false,clipboard.HasComponents()))Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); BeginObjectEdit(w,obj.GetID());clipboard.PasteComponents(obj,w.registry);});
            if(ImGui::MenuItem(MonkeyScriptKind(name)?"Delete Monkey Script":"Delete component",nullptr,false,name!="Transform"))if(auto* type=w.registry.Find(name)){BeginObjectEdit(w,obj.GetID());type->remove(obj);}
            ImGui::EndDisabled();ImGui::EndPopup();
        }
    }
    static const char* ComponentCategory(const std::string& name){
        if(MonkeyScriptKind(name)||name=="PlayerController"||name=="YouTube Listener")return "Scripts";
        if(name=="MeshRenderer"||name=="SpriteRenderer"||name=="Material"||name=="Light"||name=="ReflectionProbe")return "Rendering";
        if(name=="Camera"||name=="Camera Follower")return "Camera";
        if(name=="BoxCollider"||name=="Rigidbody")return "Physics";
        if(name=="UIText"||name=="UIButton")return "UI";
        if(name=="AudioSource")return "Audio";
        if(name=="SpriteAnimator")return "Animation";
        return "Other";
    }
    void ComponentFooter(Workspace& w,GameObject& obj,const std::string& name){
        const auto origin=ImGui::GetCursorScreenPos();
        const float width=ImGui::GetContentRegionAvail().x;
        const float height=ImGui::GetTextLineHeight();
        // Keep the same thin hit area when hidden, so hovering never shifts the cards.
        // An active button must not hide itself before the mouse-release click is processed.
        const bool hovered=ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)&&ImGui::IsMouseHoveringRect(origin,{origin.x+width,origin.y+height});
        if(hovered||ImGui::IsPopupOpen("Add component")){
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(4,0));
            ImGui::BeginDisabled(w.editor.playing);
            if(ImGui::Button("+",{26,height}))ImGui::OpenPopup("Add component");
            ImGui::SetItemTooltip("Add component");
            ImGui::SameLine();ImGui::BeginDisabled(name=="Transform");
            if(ImGui::Button("-",{26,height}))if(auto* type=w.registry.Find(name)){BeginObjectEdit(w,obj.GetID());type->remove(obj);}
            ImGui::EndDisabled();
            ImGui::SetItemTooltip(name=="Transform"?"Transform is required":MonkeyScriptKind(name)?"Detach Monkey Script":"Detach component");
            ImGui::PopStyleVar();
            if(ImGui::BeginPopup("Add component")){
                for(auto category:{"Rendering","Camera","Physics","Scripts","UI","Audio","Animation","Other"}){
                    if(ImGui::BeginMenu(category)){
                        if(std::string_view(category)=="Scripts"&&ImGui::BeginMenu("Monkey Script")){
                            for(auto& type:w.registry.types)if(MonkeyScriptKind(type.name)){
                                const bool attached=type.get(obj)!=nullptr;
                                if(ImGui::MenuItem(MonkeyScriptKind(type.name),nullptr,attached,!attached))
                                    Guard(w,[&]{BeginObjectEdit(w,obj.GetID());type.add(obj);});
                            }
                            ImGui::EndMenu();
                        }
                        for(auto& type:w.registry.types)if(!MonkeyScriptKind(type.name)&&std::string_view(ComponentCategory(type.name))==category){
                            bool attached=type.get(obj)!=nullptr;
                            if(ImGui::MenuItem(type.name.c_str(),nullptr,attached,!attached))Guard(w,[&]{BeginObjectEdit(w,obj.GetID());type.add(obj);});
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();
        }else ImGui::Dummy({width,height});
    }
    void DrawPlaybackControls(Workspace& w){
        if(ImGui::Button(w.editor.playing?"Stop":"Play")){
            Guard(w,[&]{FinishEdit(w);shapeEditor.Close(w.editor);if(w.editor.playing)w.Stop();else w.Play();});
        }
        ImGui::SameLine();ImGui::BeginDisabled(!w.editor.playing);
        if(ImGui::Button(w.editor.paused?"Resume":"Pause"))Guard(w,[&]{if(w.editor.paused)w.Resume();else w.Pause();});
        ImGui::EndDisabled();
    }
    void SaveEditorSettings(){
        std::ofstream out(fs::u8path(editorSettingsPath));
        if(!out||!(out<<std::quoted(scriptEditor)))throw std::runtime_error("Cannot save editor settings");
    }
    void InitializeLayout(const fs::path& binaries){
        RICE_BENCHMARK_FUNCTION();
        if(layoutInitialized)return;
        installRoot=InstallationRoot(binaries);
        auto settingsDir=installRoot/"Engine"/"Settings";
        fs::create_directories(settingsDir);
        projectLocation=SceneLabel(installRoot/"Projects");
        layoutPath=SceneLabel(settingsDir/"EditorLayout.ini");
        if(fs::exists(fs::u8path(layoutPath)))ImGui::LoadIniSettingsFromDisk(layoutPath.c_str());else resetLayout=true;
        ImGui::GetIO().IniFilename=layoutPath.c_str();editorSettingsPath=SceneLabel(settingsDir/"EditorSettings.txt");
        std::ifstream settings(fs::u8path(editorSettingsPath));if(settings)settings>>std::quoted(scriptEditor);
        layoutInitialized=true;
    }
    void DrawWindowMenu(){
        RICE_BENCHMARK_FUNCTION();
        if(!ImGui::BeginMenu("Window"))return;
        ImGui::MenuItem("Hierarchy",nullptr,&hierarchyOpen);ImGui::MenuItem("Inspector",nullptr,&inspectorOpen);
        ImGui::MenuItem("Scene",nullptr,&sceneOpen);ImGui::MenuItem("Game",nullptr,&gameOpen);
        ImGui::MenuItem("Project",nullptr,&projectOpen);ImGui::MenuItem("Console",nullptr,&consoleOpen);
        ImGui::MenuItem("Input",nullptr,&inputOpen);ImGui::MenuItem("Benchmark",nullptr,&benchmarkOpen);
        if(ImGui::MenuItem("Save layout"))ImGui::SaveIniSettingsToDisk(layoutPath.c_str());
        if(ImGui::MenuItem("Reset layout")){resetLayout=true;hierarchyOpen=inspectorOpen=projectOpen=consoleOpen=inputOpen=sceneOpen=gameOpen=true;}
        ImGui::EndMenu();
    }
    void DrawSettingsMenu(Workspace* workspace){
        if(!ImGui::BeginMenu("Settings"))return;
        if(ImGui::BeginMenu("Scene",workspace!=nullptr)){
            DrawSceneSettings(*workspace);
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Streaming",workspace!=nullptr)){
            ImGui::MenuItem("YouTube Commands",nullptr,&streamingPanel.commandsOpen);
            ImGui::MenuItem("Cycle",nullptr,&streamingPanel.cycleOpen);
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Script editor")){
            ImGui::TextWrapped("%s",scriptEditor.empty()?"System default":scriptEditor.c_str());
            try {
                if(ImGui::MenuItem("Choose application...")){
                    if(auto path=PickScriptEditor()){scriptEditor=SceneLabel(*path);SaveEditorSettings();}
                }
                if(ImGui::MenuItem("System default",nullptr,scriptEditor.empty())){scriptEditor.clear();SaveEditorSettings();}
            }catch(const std::exception& error){
                if(workspace)workspace->Report(error);
                else ReportOpenError(error.what());
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    static void AssetPicker(Workspace& w,const char* label,std::string& id,const BeforeEditorEdit& beforeEdit={}){
        RICE_BENCHMARK_FUNCTION();
        std::string display="None";for(auto& e:w.project->assets.Entries())if(e.id==id)display=SceneLabel(e.path);
        if(ImGui::BeginCombo(label,display.c_str())){if(ImGui::Selectable("None",id.empty())&&!id.empty()){if(beforeEdit)beforeEdit();id.clear();}for(auto& e:w.project->assets.Entries())if(ImGui::Selectable(SceneLabel(e.path).c_str(),e.id==id)&&id!=e.id){if(beforeEdit)beforeEdit();id=e.id;}ImGui::EndCombo();}
    }
public:
    struct ProjectRequest { fs::path path; bool create; };
    void RequestExit(){QueueProjectAction(ProjectAction::Exit);}
private:
    void QueueProjectAction(ProjectAction action,fs::path path={}){
        if(!pendingProject)pendingProject=PendingProject{action,std::move(path)};
    }
    CloseResult CompleteProjectAction(Workspace& w){
        auto action=pendingProject->action;
        // Open and New validate the destination before replacing the workspace.
        // Keep every draft if the destination fails to load or create.
        auto stopPreview=[this]{streamingPanel.StopPreview();};
        if(action==ProjectAction::Open)w.Open(pendingProject->path,stopPreview);
        if(action==ProjectAction::New)w.New(pendingProject->path,stopPreview);
        ResetSceneInteraction(w);scriptPanel.CloseProject();streamingPanel.CloseProject();
        pendingProject.reset();closeError.clear();confirmingClose=false;
        selectedPrefab.clear();prefabRoot=0;exportProject.clear();projectOpenError.clear();
        if(action==ProjectAction::Exit)return CloseResult::Exit;
        if(action==ProjectAction::Close)return CloseResult::CloseProject;
        return CloseResult::None;
    }
    CloseResult ProjectCloseDialog(Workspace& w){
        if(!pendingProject)return CloseResult::None;
        if(!confirmingClose){
            try{
                FinishEdit(w);gizmo.Commit(w.editor);shapeEditor.Commit(w.editor);
                if(!w.HasUnsavedChanges()&&!shapeEditor.HasUnsavedChanges()&&
                   !scriptPanel.HasUnsavedChanges(w)&&!streamingPanel.HasUnsavedChanges()){
                    try{return CompleteProjectAction(w);}
                    catch(const std::exception& error){pendingProject.reset();w.Report(error);return CloseResult::None;}
                }
            }catch(const std::exception& error){closeError=error.what();}
            confirmingClose=true;
        }
        constexpr const char* title="Unsaved project changes";
        ImGui::OpenPopup(title,ImGuiPopupFlags_NoReopen);
        CloseResult result=CloseResult::None;
        if(ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            auto name=SceneLabel(w.project->FilePath().filename());
            ImGui::Text("Save changes to %s before closing?",name.c_str());
            if(!closeError.empty())ImGui::TextWrapped("%s",closeError.c_str());
            bool save=ImGui::Button("Save");ImGui::SameLine();
            bool discard=ImGui::Button("Don't Save");ImGui::SameLine();
            if(ImGui::Button("Go Back")||ImGui::IsKeyPressed(ImGuiKey_Escape)){
                pendingProject.reset();confirmingClose=false;closeError.clear();ImGui::CloseCurrentPopup();
            }else if(save||discard){
                try{
                    if(save){
                        scriptPanel.SaveBeforeClose(w);
                        if(shapeEditor.HasUnsavedChanges())shapeEditor.SaveInScene(w.editor,&w.project->assets);
                        streamingPanel.SaveBeforeClose(*w.editor.scene);
                        w.SaveBeforeClose();
                    }
                    result=CompleteProjectAction(w);
                    ImGui::CloseCurrentPopup();
                }catch(const std::exception& error){closeError=error.what();w.Report(error);}
            }
            ImGui::EndPopup();
        }
        return result;
    }
    std::optional<ProjectRequest> NewProjectDialog(){
        if(requestNewProject){
            requestNewProject=false;
            newProjectName="New Project";
            ImGui::OpenPopup("New Project");
        }
        std::optional<ProjectRequest> selected;
        if(ImGui::BeginPopupModal("New Project",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
            EditString("Project name",newProjectName);
            EditString("Location",projectLocation);
            const bool valid=!newProjectName.empty()&&newProjectName!="."&&newProjectName!=".."&&
                newProjectName.find_first_of("<>:\"/\\|?*")==std::string::npos&&
                newProjectName.back()!='.'&&newProjectName.back()!=' '&&!projectLocation.empty();
            ImGui::BeginDisabled(!valid);
            if(ImGui::Button("Create")){
                selected=ProjectRequest{fs::u8path(projectLocation)/fs::u8path(newProjectName),true};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();ImGui::SameLine();
            if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return selected;
    }
public:
    std::optional<ProjectRequest> DrawEmpty(const fs::path& binaries){
        RICE_BENCHMARK_FUNCTION();
        InitializeLayout(binaries);std::optional<ProjectRequest> selected;
        auto pick=[&]{try{if(auto path=PickProjectFile(installRoot/"Projects"))selected=ProjectRequest{*path,false};projectOpenError.clear();}
            catch(const std::exception& error){projectOpenError=error.what();}};
        if(ImGui::BeginMainMenuBar()){
            if(ImGui::BeginMenu("File")){
                if(ImGui::MenuItem("New Project...","Ctrl+N"))requestNewProject=true;
                if(ImGui::MenuItem("Open Project...","Ctrl+O"))pick();
                ImGui::MenuItem("Import...",nullptr,false,false);
                ImGui::MenuItem("Save",nullptr,false,false);ImGui::EndMenu();
            }
            DrawWindowMenu();
            DrawSettingsMenu(nullptr);
            ImGui::BeginDisabled();ImGui::Button("Import...");ImGui::SameLine();ImGui::Button("Play");ImGui::SameLine();ImGui::Button("Pause");ImGui::EndDisabled();
            ImGui::SameLine();ImGui::TextUnformatted("No project loaded");ImGui::EndMainMenuBar();
        }
        if(ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_O,false))pick();
        if(ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_N,false))requestNewProject=true;
        auto dock=ImGui::DockSpaceOverViewport(ImGui::GetID("RiceDockSpace"),ImGui::GetMainViewport());
        if(resetLayout){DefaultLayout(dock);resetLayout=false;}
        auto empty=[](const char* title,bool& open){if(open){ImGui::Begin(title,&open);ImGui::End();}};
        empty("Hierarchy",hierarchyOpen);empty("Inspector###Inspector",inspectorOpen);
        empty("Scene",sceneOpen);empty("Game",gameOpen);empty("Input",inputOpen);empty("Benchmark",benchmarkOpen);
        if(projectOpen){ImGui::Begin("Project",&projectOpen);
            ImGui::BeginDisabled();ImGui::Button("Import...");ImGui::EndDisabled();
            ImGui::TextWrapped("Use File > New Project or Open Project to start importing assets.");
            if(!projectOpenError.empty())ImGui::TextWrapped("%s",projectOpenError.c_str());ImGui::End();}
        if(consoleOpen){ImGui::Begin("Console",&consoleOpen);if(!projectOpenError.empty())ImGui::TextWrapped("%s",projectOpenError.c_str());ImGui::End();}
        if(auto created=NewProjectDialog())return created;
        return selected;
    }
    void ReportOpenError(const std::string& error){RICE_BENCHMARK_FUNCTION();projectOpenError=error;}
    PerspectiveCamera& SceneCamera(int width,int height){ RICE_BENCHMARK_FUNCTION(); return editorCamera.Get(float(width),float(height));}
    bool CapturesGameInput()const{ RICE_BENCHMARK_FUNCTION(); return !gameOpen||!gameFocused;}
    ~SceneInspector(){ RICE_BENCHMARK_FUNCTION(); if(layoutInitialized&&ImGui::GetCurrentContext()){ImGui::SaveIniSettingsToDisk(layoutPath.c_str());ImGui::GetIO().IniFilename=nullptr;}}
    CloseResult Draw(Workspace& w,const fs::path& binaries,const std::function<Matrix4(int,int,bool)>& render){
        RICE_BENCHMARK_FUNCTION();
        InitializeLayout(binaries);
        if(exportProject!=w.project->root){
            exportProject=w.project->root;
            auto filename=w.project->FilePath().filename().replace_extension(".exe");
            exportPath=SceneLabel(AvailableBuildFile(installRoot/"Builds"/filename));
        }
        auto pickProject=[&]{Guard(w,[&]{if(auto path=PickProjectFile(installRoot/"Projects"))QueueProjectAction(ProjectAction::Open,*path);});};
        auto importAsset=[&]{Guard(w,[&]{
            auto path=PickAssetFile();
            if(!path)return;
            if(_wcsicmp(path->extension().c_str(),L".fbx")==0){
                FinishEdit(w);
                auto root=w.ImportFbx(*path);
                editorCamera.FrameHierarchy(*w.editor.scene,root);
            }else{
                w.project->assets.Import(*path);
                w.Log("Imported asset: "+SceneLabel(path->filename()));
            }
        });};
        w.editor.SyncSelection();
        Shortcuts(w);
        if(!pendingProject&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_O,false))pickProject();
        if(!pendingProject&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_N,false))requestNewProject=true;
        if(!pendingProject&&!scriptPanel.Focused()&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_S,false)&&!w.editor.playing)Guard(w,[&]{FinishEdit(w);shapeEditor.Commit(w.editor);w.Save();});
        w.editor.SyncSelection();
        if(ImGui::BeginMainMenuBar()){
            if(ImGui::BeginMenu("File")){
                if(ImGui::MenuItem("New Project...","Ctrl+N"))requestNewProject=true;
                if(ImGui::MenuItem("Open Project...","Ctrl+O"))pickProject();
                if(ImGui::MenuItem("Import..."))importAsset();
                if(ImGui::MenuItem("Save","Ctrl+S",false,!w.editor.playing))Guard(w,[&]{FinishEdit(w);shapeEditor.Commit(w.editor);w.Save();});
                if(ImGui::MenuItem("Close Project"))QueueProjectAction(ProjectAction::Close);
                if(ImGui::MenuItem("Exit"))RequestExit();
                ImGui::EndMenu();
            }
            DrawWindowMenu();
            DrawSettingsMenu(&w);
        if(ImGui::Button("Import..."))importAsset();
        ImGui::SameLine();
        ImGui::TextUnformatted("Movable");ImGui::SameLine();
        auto toggle=[](const char* label,bool& enabled){ RICE_BENCHMARK_FUNCTION(); ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(enabled?ImGuiCol_ButtonActive:ImGuiCol_Button));bool clicked=ImGui::Button(label);ImGui::PopStyleColor();if(clicked)enabled=!enabled;return clicked;};
        if(toggle("Camera",moveView)){cameraGesture.Cancel();wheelGesture.Cancel();}
        ImGui::SameLine();if(toggle("Object",moveObject)){if(!moveObject)gizmo.Cancel(w.editor);cameraGesture.Cancel();wheelGesture.Cancel();}
        ImGui::SameLine();
        DrawPlaybackControls(w);
        ImGui::SameLine();if(ImGui::Button("Save"))Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); FinishEdit(w);shapeEditor.Commit(w.editor);w.Save();});
        ImGui::SameLine();if(ImGui::Button("Undo")){Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); FinishEdit(w);shapeEditor.Commit(w.editor);w.editor.Undo();});}
        ImGui::SameLine();if(ImGui::Button("Redo")){Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); FinishEdit(w);shapeEditor.Commit(w.editor);w.editor.Redo();});}
            ImGui::SameLine();ImGui::TextUnformatted(w.editor.playing?(w.editor.paused?"Paused":"Playing - click Game to control"):moveView?"WASD move | Q down / E up | L-drag pan | R-drag look":"Edit mode");ImGui::EndMainMenuBar();
        }
        // Run after shortcuts and toolbar commands, before drawing any warning;
        // this also observes Undo/Redo requested outside the inspector.
        SyncMaterialPrompt(w);
        ImGuiID dock=ImGui::DockSpaceOverViewport(ImGui::GetID("RiceDockSpace"),ImGui::GetMainViewport());
        if(resetLayout){DefaultLayout(dock);resetLayout=false;}
        benchmarkPanel.Draw(benchmarkOpen);
        streamingPanel.Draw(w.project->root,SceneLabel(w.project->ActiveScene()),*w.editor.scene,w.editor.selected,[&]{if(w.editor.selected)BeginObjectEdit(w,w.editor.selected);});
        gameFocused=false;sceneDrawn=false;
        View(w,"Scene",sceneOpen,sceneTarget,false,render);View(w,"Game",gameOpen,gameTarget,true,render);
        if(!sceneDrawn){gizmo.Cancel(w.editor);cameraGesture.Cancel();wheelGesture.Cancel();}
        if(hierarchyOpen){ImGui::Begin("Hierarchy",&hierarchyOpen);
        SceneControls(w);
        if(ImGui::Button("Create object"))ImGui::OpenPopup("Create object menu");
        if(ImGui::BeginPopup("Create object menu")) {
            auto create=[&](const char* name,int shape,int script,bool sprite) {
                RICE_BENCHMARK_FUNCTION();
                pendingObjectAction=[&,name,shape,script,sprite] {
                    RICE_BENCHMARK_FUNCTION();
                    w.editor.Edit([&](Scene& scene) {
                        RICE_BENCHMARK_FUNCTION();
                        if(script==2)if(auto* manager=SingletonManager::Instance(scene)) {
                            w.editor.Select(manager->GetGameObject().GetID());
                            return;
                        }
                        auto& object=scene.CreateGameObject(name);
                        if(shape>=0)object.AddComponent<MeshRenderer>().SetShape(shape);
                        if(sprite)object.AddComponent<SpriteRenderer>();
                        if(script==4)object.AddComponent<CameraComponent>();
                        if(script==1)object.AddComponent<ObjectScript>();
                        if(script==2)object.AddComponent<SingletonManager>();
                        if(script==3) {
                            object.AddComponent<BlankScript>();
                            object.AddComponent<BoxCollider>();
                        }
                        w.editor.Select(object.GetID());
                    });
                };
                ImGui::CloseCurrentPopup();
            };
            if(ImGui::Selectable("Camera"))create("Camera",-1,4,false);
            if(ImGui::BeginTabBar("Object types")) {
                if(ImGui::BeginTabItem("Basic shapes")) {
                    const char* names[]={"Cube","Circle","Rectangle","Sphere","Cylinder","Cone","Custom"};
                    for(int i=0;i<7;++i)if(ImGui::Selectable(names[i]))create(names[i],i,0,false);
                    ImGui::EndTabItem();
                }
                if(ImGui::BeginTabItem("Sprite")) {
                    if(ImGui::Selectable("2D Image"))create("Sprite",-1,0,true);
                    ImGui::TextUnformatted("Choose an image in Inspector.");
                    ImGui::EndTabItem();
                }
                if(ImGui::BeginTabItem("Monkey Script")) {
                    if(ImGui::Selectable("Object"))create("Object",-1,1,false);
                    if(ImGui::Selectable("Singleton Manager"))create("Singleton Manager",-1,2,false);
                    if(ImGui::Selectable("Blank Script"))create("Blank Script",-1,3,false);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndPopup();
        }

        ImGui::Text("Selected: %zu",w.editor.selection.size());
        DrawHierarchy(w);
        ImGui::End();}
        auto* inspected=w.editor.scene->Find(w.editor.selected);
        bool editedShape=inspected&&HasTriangleMaterials(*inspected);
        if(inspectorOpen){ImGui::Begin(editedShape?"Inspector (edited in shape editor)###Inspector":"Inspector###Inspector",&inspectorOpen);
        if(auto* obj=w.editor.scene->Find(w.editor.selected)){
            if(ImGui::Button("Duplicate")){auto id=obj->GetID();pendingObjectAction=[&,id]{ RICE_BENCHMARK_FUNCTION(); EditorClipboard copy;copy.CopyObject(*w.editor.scene,id,w.registry);copy.PasteObject(w.editor,w.registry);};}
            ImGui::SameLine();if(ImGui::Button("Delete"))pendingObjectAction=[&]{w.editor.DeleteSelection();};
            if(!obj->IsPendingDestroy()){
                BeforeEditorEdit beforeEdit=[&]{BeginObjectEdit(w,obj->GetID());};
                EditString("Name",obj->name,beforeEdit);EditValue(obj->activeSelf,beforeEdit,[](bool& next){return ImGui::Checkbox("Active",&next);});
                if(ImGui::BeginCombo("Parent",obj->GetHierarchyParent()?obj->GetHierarchyParent()->name.c_str():"Root")){
                    auto reparent=[&](EntityID parent){auto id=obj->GetID();pendingObjectAction=[&,id,parent]{w.editor.Edit([&](Scene& scene){EditorGroups::Reparent(*scene.Find(id),scene.Find(parent));});};};
                    if(ImGui::Selectable("Root"))reparent(0);
                    for(auto& candidate:w.editor.scene->GetObjects())if(candidate.get()!=obj&&ImGui::Selectable(candidate->name.c_str()))reparent(candidate->GetID());ImGui::EndCombo();
                }
                if(obj->isGroup){auto [a,b]=GizmoMath::SpaceBounds(*obj,Matrix4(1));
                    auto size=b-a;ImGui::Text("Group world size: %.3f, %.3f, %.3f",size.x,size.y,size.z);
                    if(ImGui::Button("Ungroup")){auto id=obj->GetID();pendingObjectAction=[&,id]{ RICE_BENCHMARK_FUNCTION(); EditorGroups::Ungroup(w.editor,id);};}
                }
                if(obj->GetGroup())ImGui::Text("Original parent: %s",obj->GetParent()?obj->GetParent()->name.c_str():"Root");
                ImGui::PushID(obj);
                ImGui::BeginChild("Transform box",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY);
                ImGui::SeparatorText("Transform");ComponentMenu(w,*obj,"Transform");
                EditValue(obj->transform.position,beforeEdit,[](Vector3& next){return ImGui::DragFloat3("Local position",&next.x,.05f);});
                EditValue(obj->transform.rotation,beforeEdit,[](Vector3& next){return ImGui::DragFloat3("Local rotation",&next.x,1);});
                EditValue(obj->transform.localScale,beforeEdit,[](Vector3& next){return ImGui::DragFloat3("Local scale",&next.x,.02f);});
                DrawPivotFields(*obj,beforeEdit);
                auto world=Vector3(obj->GetWorldMatrix()[3]);if(ImGui::DragFloat3("World position",&world.x,.05f))Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); beforeEdit();obj->TranslateWorld(world-Vector3(obj->GetWorldMatrix()[3]));});
                ComponentFooter(w,*obj,"Transform");ImGui::EndChild();
                for(auto& type:w.registry.types){ImGui::PushID(type.name.c_str());if(auto* component=type.get(*obj)){
                    bool protectedMaterial=type.name=="Material"&&HasTriangleMaterials(*obj),materialProceeded=false;
                    auto originalMaterial=MaterialSettings(obj->GetComponent<Material>());
                    TriangleMaterials originalTriangles;if(protectedMaterial)originalTriangles=obj->GetComponent<MeshRenderer>()->triangleMaterials;
                    ImGui::BeginChild("Component box",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY);
                    auto label=protectedMaterial?std::string("Material (edited in shape editor)###Material"):MonkeyScriptKind(type.name)?std::string("Monkey Script"):type.name;
                    bool expanded=ImGui::CollapsingHeader(label.c_str(),ImGuiTreeNodeFlags_DefaultOpen);
                    if(type.name!="Material"||!materialPrompt.Pending(*w.editor.scene,*obj))ComponentMenu(w,*obj,type.name);
                    if(expanded&&(component=type.get(*obj))){
                        if(MonkeyScriptKind(type.name)){
                            auto& script=static_cast<ScriptComponentBase&>(*component);
                            DrawProperty("Enabled",&script.enabled,beforeEdit);
                            ImGui::TextDisabled("%s",MonkeyScriptKind(type.name));
                            if(!script.scriptFile.empty())ImGui::TextWrapped("%s",script.scriptFile.c_str());
                            const bool error=scriptPanel.HasError(w,obj->GetID(),type.name);
                            if(error)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1.f,.30f,.30f,1.f));
                            if(ImGui::Button("Open RiceMonkey"))Guard(w,[&]{scriptPanel.Open(w,*obj,type.name);});
                            if(error)ImGui::PopStyleColor();
                        }
                        else if(type.name=="Material"){
                            materialProceeded=DrawMaterialInspector(*w.editor.scene,*obj,&w.project->assets,materialPrompt,beforeEdit);
                            if(materialProceeded)shapeEditor.SyncMaterialOverwrite(w.editor,obj->GetID());
                        }
                        else if(type.name=="Anchor")DrawAnchorFields(*obj->GetComponent<Anchor>(),*w.editor.scene,obj->GetID(),true,beforeEdit);
                        else if(type.name=="Camera")DrawCameraFields(*obj->GetComponent<CameraComponent>(),beforeEdit);
                        else if(type.name=="Camera Follower")DrawCameraFollowerFields(*obj->GetComponent<CameraFollower>(),*w.editor.scene,obj->GetID(),beforeEdit);
                        else type.fields(*component,[&](const char* name,Property p){
                            RICE_BENCHMARK_FUNCTION();
                            if(type.name=="Light"&&std::string(name)=="Type")EditValue(obj->GetComponent<Light>()->type,beforeEdit,[](int& next){return ImGui::Combo("Type",&next,"Point\0Directional\0");});
                            else if(type.name=="MeshRenderer" && std::string_view(name)=="Shape"){
                                auto* mesh=obj->GetComponent<MeshRenderer>();int next=mesh->shape;
                                if(ImGui::Combo("Shape",&next,"Cube\0Circle\0Rectangle\0Sphere\0Cylinder\0Cone\0Custom\0")){beforeEdit();mesh->SetShape(next);}
                                if(mesh->shape==int(BasicShape::Custom)){
                                    ImGui::BeginDisabled(w.editor.playing);
                                    if(ImGui::Button("Open Shape Editor")){auto id=obj->GetID();pendingObjectAction=[this,&w,id]{shapeEditor.Open(w.editor,id,w.project->assets.Root());};}
                                    ImGui::EndDisabled();
                                }
                            }
                            else if(type.name=="Material" && std::holds_alternative<float*>(p))
                                EditValue(*std::get<float*>(p),beforeEdit,[&](float& next){return ImGui::SliderFloat(name,&next,0.f,1.f,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                            else if(type.name=="Material" && std::string_view(name)=="Albedo asset")
                                {
                                    AssetPicker(w,"Albedo / Cubemap",*std::get<std::string*>(p),beforeEdit);
                                    auto* material=obj->GetComponent<Material>();
                                    if(material->albedoTexture)ImGui::Text("Detected: %s",CubemapLayoutName(DetectCubemapLayout(material->albedoTexture->GetWidth(),material->albedoTexture->GetHeight())));
                                }
                            else if(type.name=="ReflectionProbe" && std::string_view(name)=="Mipmap storage quality")
                                EditValue(*std::get<int*>(p),beforeEdit,[&](int& next){return ImGui::Combo(name,&next,"RGB8 (low memory)\0RGB16F (HDR)\0");});
                            else if(type.name=="ReflectionProbe" && std::string_view(name)=="Cubemap resolution"){
                                auto* value=std::get<int*>(p);
                                if(ImGui::BeginCombo(name,std::to_string(*value).c_str())){
                                    for(int resolution:{16,32,64,128,256,512,1024,2048})
                                        if(ImGui::Selectable(std::to_string(resolution).c_str(),*value==resolution)&&*value!=resolution){beforeEdit();*value=resolution;}
                                    ImGui::EndCombo();
                                }
                            }
                            else if(type.name=="ReflectionProbe" && std::string_view(name)=="Max object size")
                                EditValue(*std::get<float*>(p),beforeEdit,[](float& next){return ImGui::DragFloat("Flat reflection below (px)",&next,1.f,0.f,100000.f,"%.0f px",ImGuiSliderFlags_AlwaysClamp);});
                            else if(type.name=="MeshRenderer"&&std::string_view(name)=="Shape asset"){
                                auto& path=obj->GetComponent<MeshRenderer>()->shapeAsset;if(!path.empty())ImGui::TextWrapped("Shape asset: %s",path.c_str());
                            }
                            else if(type.name=="YouTube Listener"&&std::string_view(name)=="Command bindings"){
                                auto* listener=obj->GetComponent<YouTubeListener>();
                                std::vector<std::string> diagnostics;
                                auto bindings=listener->BuildBindings(&diagnostics);
                                ImGui::Text("YouTube commands: %zu",bindings.size());
                                for(const auto& binding:bindings)ImGui::TextWrapped("%s -> %s",binding.pattern.Text().c_str(),binding.pattern.Function().functionId.c_str());
                                for(const auto& message:diagnostics)ImGui::TextWrapped("%s",message.c_str());
                            }
                            else DrawProperty(name,p,beforeEdit);
                        });
                        if(type.name=="SpriteRenderer")AssetPicker(w,"Choose image",obj->GetComponent<SpriteRenderer>()->asset,beforeEdit);
                        if(type.name=="AudioSource") {auto& source=*obj->GetComponent<AudioSource>();AssetPicker(w,"Choose audio",source.clip,beforeEdit);if(ImGui::Button("Play audio"))Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); w.PlayAudio(source);});ImGui::SameLine();if(ImGui::Button("Pause"))Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); source.Pause(true);});ImGui::SameLine();if(ImGui::Button("Resume"))Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); source.Pause(false);});ImGui::SameLine();if(ImGui::Button("Stop audio"))source.Stop();}
                        if(type.name=="BoxCollider")ImGui::Text("Overlap: %s",obj->GetComponent<BoxCollider>()->overlapping?"Yes":"No");

                    }
                    if(type.get(*obj)&&(type.name!="Material"||!materialPrompt.Pending(*w.editor.scene,*obj)))ComponentFooter(w,*obj,type.name);
                    // Context-menu paste and detach must obey the same protection
                    // as the material fields; restore them while the prompt waits.
                    if(protectedMaterial&&!materialProceeded){
                        auto* material=obj->GetComponent<Material>();auto* mesh=obj->GetComponent<MeshRenderer>();
                        if(!material||!(MaterialSettings(material)==originalMaterial)||(mesh&&mesh->triangleMaterials.faces!=originalTriangles.faces)){
                            auto proposed=MaterialSettings(material);materialPrompt.Queue(*w.editor.scene,*obj,proposed,!material);
                            ApplyMaterialSettings(obj->AddComponent<Material>(),originalMaterial);
                            if(mesh)mesh->triangleMaterials=std::move(originalTriangles);ImGui::ClearActiveID();
                        }
                    }
                    ImGui::EndChild();
                }ImGui::PopID();}
                ImGui::PopID();
            }
        }
        ImGui::End();}
        EndFrameEdits(w);
        if(pendingObjectAction){FinishEdit(w);auto action=std::move(pendingObjectAction);pendingObjectAction=nullptr;Guard(w,action);}
        if(projectOpen){ImGui::Begin("Project",&projectOpen);
        if(ImGui::Button("Import..."))importAsset();
        ImGui::SameLine();if(ImGui::Button("Refresh"))Guard(w,[&]{w.project->assets.Refresh();});
        PrefabControls(w);
        if(ImGui::CollapsingHeader("Project / Export")){
            EditString("Build file",exportPath);
            if(ImGui::Button("Build standalone game"))Guard(w,[&]{
                RICE_BENCHMARK_FUNCTION();
                if(w.editor.playing)throw std::runtime_error("Stop before building");
                auto path=fs::u8path(exportPath);
                if(path.filename().empty())throw std::runtime_error("Enter a game executable filename.");
                auto ext=path.extension().wstring();
                if(!ext.empty()&&_wcsicmp(ext.c_str(),L".exe")!=0)throw std::runtime_error("Choose a Windows .exe filename.");
                path.replace_extension(".exe");
                if(fs::exists(path))throw std::runtime_error("Build file already exists. Choose a new filename.");
                w.project->Export(*w.editor.scene,w.registry,binaries,path);
                w.Log("Build complete: "+SceneLabel(path));
                auto filename=w.project->FilePath().filename().replace_extension(".exe");
                exportPath=SceneLabel(AvailableBuildFile(path.parent_path()/filename));
            });
        }
        EditString("New relative filename",renamePath);
        // Renaming invalidates the registry vector; apply after iteration.
        std::string renameID;
        for(auto& e:w.project->assets.Entries()){ImGui::PushID(e.id.c_str());ImGui::TextUnformatted(SceneLabel(e.path).c_str());ImGui::SameLine();if(ImGui::SmallButton("Rename"))renameID=e.id;ImGui::PopID();}
        if(!renameID.empty())Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); w.project->assets.Rename(renameID,fs::u8path(renamePath));});
        ImGui::End();}
        if(inputOpen){ImGui::Begin("Input",&inputOpen);
        if(ImGui::CollapsingHeader("Input actions (GLFW key codes)",ImGuiTreeNodeFlags_DefaultOpen)){for(auto& [name,key]:w.project->input.keys)ImGui::DragInt(name.c_str(),&key,1,32,348);ImGui::TextWrapped("A=65 D=68 W=87 S=83 Space=32. Save project to persist.");EditString("New action",newAction);ImGui::DragInt("Key code",&newKey,1,32,348);if(ImGui::Button("Add / rebind action"))Guard(w,[&]{ RICE_BENCHMARK_FUNCTION(); w.project->input.Bind(newAction,newKey);});}
        ImGui::End();}
        if(consoleOpen){ImGui::Begin("Console",&consoleOpen);
        ImGui::Text("FPS %.1f | update %.3f ms | objects %zu",ImGui::GetIO().Framerate,w.updateMilliseconds,w.editor.scene->GetObjectCount());
        if(ImGui::Button("Clear log")){Debug::ClearMessages();w.logs.clear();}
        ImGui::SameLine();ImGui::Checkbox("Auto-scroll",&autoScrollLogs);
        auto messages=Debug::GetMessages();
        ImGui::BeginChild("Log messages",{0,0},false,ImGuiWindowFlags_HorizontalScrollbar);
        for(const auto& entry:messages){
            ImVec4 color=entry.level>=spdlog::level::err?ImVec4(1,.4f,.4f,1):entry.level==spdlog::level::warn?ImVec4(1,.8f,.3f,1):ImVec4(.85f,.85f,.85f,1);
            ImGui::PushStyleColor(ImGuiCol_Text,color);ImGui::TextUnformatted(entry.text.c_str());ImGui::PopStyleColor();
        }
        if(autoScrollLogs&&!messages.empty()&&messages.back().sequence!=lastLogSequence)ImGui::SetScrollHereY(1.f);
        lastLogSequence=messages.empty()?0:messages.back().sequence;
        ImGui::EndChild();ImGui::End();}
        shapeEditor.Draw(w.editor,&w.project->assets);
        scriptPanel.Draw(w,scriptEditor);
        if(auto created=NewProjectDialog())QueueProjectAction(ProjectAction::New,created->path);
        return ProjectCloseDialog(w);
    }
};
}
