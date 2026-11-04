#pragma once
#include "ShapeEditorMath.h"
#include "EditorCamera.h"
#include "Project/SceneCodec.h"
#include "Project/ShapeFile.h"
#include "Platform/FileDialog.h"
#include "PivotAnchorUI.h"
#include "MaterialUI.h"
#include "Renderer/ViewportTarget.h"
#include <imgui.h>

namespace Rice {
class ShapeEditor {
public:
    using FilePicker=std::function<std::optional<fs::path>(bool,const fs::path&)>;
private:
    bool open=false,focusNext=false,focused=false,cameraMode=false,boxing=false,coordinateEdit=false;
    bool vertexListOpen=false,generalOpen=false,generalEdit=false,dragAnchor=false,followerSelected=false,dragFollower=false;
    bool trianglePanelOpen=false,surfaceEdit=false;
    int hoveredTriangle=-1;
    enum class Command {None,InScene,AsAsset,Elsewhere,Load,Undo,Redo};
    ComponentRegistry draftRegistry;
    EditorSession draft{draftRegistry};
    FilePicker filePicker;
    fs::path filePath,defaultDirectory;
    std::string savedData;
    std::string appliedMaterials;
    ShapeDocument startDocument;
    EntityID startChild=0;
    std::string fileError;
    EntityID target=0;
    uint64_t document=0,sceneInstance=0,historyRevision=0,spatialRevision=0;
    ShapeSelection selection;
    MeshIntersection spatial;
    EditorCamera camera;
    Vector3 cameraPivot{0};
    CameraWheelGesture wheel;
    CameraPointerGesture pointer;
    ViewportTarget viewport;
    Scope<SceneRenderer> renderer;
    int dragButton=-1,dragAxis=0;
    uint32_t dragPivot=0;
    float dragSign=1,unitsPerPixel=1;
    Vector2 dragStart{0},lastDragMouse{0},dragDirection{0},boxStart{0};
    std::set<uint32_t> boxBase,dragSelection;
    std::vector<Vector3> startPositions;
    std::optional<EditorSession::EditSnapshot> snapshot;
    ImGuiID coordinateItem=0;
    Vector2 canvasOrigin{0},canvasSize{0};

    static ImVec2 Point(Vector2 p){return {p.x,p.y};}
    static std::string PathLabel(const fs::path& path){
        auto utf8=path.generic_u8string();return {utf8.begin(),utf8.end()};
    }
    MeshRenderer* Mesh(EditorSession& editor)const {
        auto* obj=editor.scene->Find(target);
        if(!obj||obj->IsPendingDestroy())return nullptr;
        auto* mesh=obj->GetComponent<MeshRenderer>();return mesh&&mesh->shape==int(BasicShape::Custom)?mesh:nullptr;
    }
    GameObject* Source(EditorSession& editor)const{
        if(!open||editor.playing||editor.documentRevision!=document||!Mesh(editor))return nullptr;
        return editor.scene->Find(target);
    }
    ShapeDocument CaptureDraft()const{return ShapeDocument::Capture(*draft.scene->Find(target));}
    std::string MaterialsData()const{std::ostringstream out;out<<std::setprecision(9);draft.scene->Find(target)->GetComponent<MeshRenderer>()->triangleMaterials.Write(out);return out.str();}
    Vector3 Pivot()const{return draft.scene->Find(target)->GetLocalPivot();}
    void Frame(const ShapeMesh& mesh){
        RICE_BENCHMARK_FUNCTION();
        auto [low,high]=mesh.Bounds();camera.yaw=-60;camera.pitch=-20;cameraPivot=Pivot();
        camera.position=(low+high)*.5f-cameraPivot-camera.Forward()*std::max(.5f,glm::length(high-low)*1.8f);
    }
    void BeginEdit(EditorSession& editor,MeshRenderer& mesh){
        RICE_BENCHMARK_FUNCTION();
        snapshot=editor.CaptureObject(target);startPositions=mesh.custom.positions;
        startDocument=CaptureDraft();auto* anchor=draft.scene->Find(target)->GetComponent<Anchor>();startChild=anchor?anchor->child:0;
    }
    void FinishEdit(EditorSession& editor,bool cancel=false){
        RICE_BENCHMARK_FUNCTION();
        if(snapshot&&sceneInstance==editor.scene->GetInstanceID()){
            if(cancel){if(auto* obj=editor.scene->Find(target)){auto oldPivot=Pivot();startDocument.Apply(*obj);camera.position+=oldPivot-Pivot();if(auto* a=obj->GetComponent<Anchor>())a->child=startChild;++editor.previewRevision;}}
            else editor.Record(*snapshot);
        }
        snapshot.reset();startPositions.clear();dragButton=-1;coordinateEdit=generalEdit=surfaceEdit=dragAnchor=dragFollower=false;coordinateItem=0;
    }
    void StartDrag(EditorSession& editor,MeshRenderer& mesh,const ShapeEditorMath::CubeFace& face,int button,Vector2 mouse,Vector2 origin,Vector2 size,const Matrix4& vp,bool anchor=false){
        RICE_BENCHMARK_FUNCTION();
        BeginEdit(editor,mesh);dragAnchor=anchor;dragButton=button;dragAxis=face.axis;dragSign=face.sign;dragStart=lastDragMouse=mouse;
        dragPivot=anchor?0:static_cast<uint32_t>(selection.ActiveVertex());dragSelection=selection.selected;
        auto center=anchor?startDocument.anchor.position:mesh.custom.positions[dragPivot]-Pivot();Vector3 axis(0);axis[dragAxis]=dragSign;
        float depth=(vp*Vector4(center,1)).w;
        unitsPerPixel=2.f*depth*std::tan(glm::radians(45.f)*.5f)/std::max(1.f,size.y);
        auto p=GizmoMath::Project(center,origin,size,vp),q=GizmoMath::Project(center+axis*unitsPerPixel*20.f,origin,size,vp);
        auto projected=p&&q?Vector2(*q-*p):Vector2(0);float length=glm::length(projected);
        // A face looking straight at the camera uses vertical motion for depth.
        dragDirection=length>2.f?projected/length:Vector2(0,-1);
        if(length>2.f)unitsPerPixel*=20.f/length;
    }
    void UpdateDrag(EditorSession& editor,MeshRenderer& mesh,Vector2 mouse){
        RICE_BENCHMARK_FUNCTION();
        if(dragButton<0)return;
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)){FinishEdit(editor,true);return;}
        if(!ImGui::IsMouseDown(dragButton)){FinishEdit(editor);return;}
        if(mouse==lastDragMouse)return;
        lastDragMouse=mouse;auto delta=mouse-dragStart;
        if(dragFollower){
            if(auto* follower=DraftObject().GetComponent<CameraFollower>()){follower->transform=startDocument.follower;follower->transform.position[dragAxis]+=glm::dot(delta,dragDirection)*unitsPerPixel;++editor.previewRevision;}
            return;
        }
        if(dragAnchor){
            if(auto* anchor=DraftObject().GetComponent<Anchor>()){
                anchor->transform=startDocument.anchor;
                if(dragButton==0)anchor->transform.position[dragAxis]+=glm::dot(delta,dragDirection)*unitsPerPixel*dragSign;
                else{Vector3 axis(0);axis[dragAxis]=1;auto rotation=glm::angleAxis(glm::radians((delta.x-delta.y)*.5f*dragSign),axis)*Quaternion(glm::radians(startDocument.anchor.rotation));anchor->transform.rotation=GizmoMath::ContinuousEuler(rotation,startDocument.anchor.rotation);}
            }
            return;
        }
        auto oldPivot=Pivot();
        if(dragButton==0){Vector3 offset(0);offset[dragAxis]=glm::dot(delta,dragDirection)*unitsPerPixel*dragSign;ShapeEditorMath::Translate(mesh.custom,startPositions,dragSelection,offset);}
        else ShapeEditorMath::Rotate(mesh.custom,startPositions,dragPivot,dragAxis,(delta.x-delta.y)*.5f*dragSign);
        camera.position+=oldPivot-Pivot();
        ++editor.previewRevision;
    }
    static void DashedLine(ImDrawList* draw,Vector2 a,Vector2 b){
        auto offset=b-a;float length=glm::length(offset);if(length<.01f)return;auto direction=offset/length;
        for(float p=0;p<length;p+=8)draw->AddLine(Point(a+direction*p),Point(a+direction*std::min(length,p+4)),IM_COL32(245,220,70,255));
    }
    void ToggleVertexList(EditorSession& editor){
        FinishEdit(editor);vertexListOpen=!vertexListOpen;
    }
    Command SaveItems(){
        Command command=Command::None;
        if(ImGui::MenuItem("in scene","Ctrl+Shift+O"))command=Command::InScene;
        if(ImGui::MenuItem("as asset","Ctrl+Shift+S",false,!filePath.empty()))command=Command::AsAsset;
        if(ImGui::MenuItem("elsewhere..."))command=Command::Elsewhere;
        return command;
    }
    Command DrawToolbar(EditorSession& editor,MeshRenderer& mesh,const std::string& objectName){
        RICE_BENCHMARK_FUNCTION();
        Command command=Command::None;
        if(ImGui::BeginMenuBar()){
            if(ImGui::BeginMenu("File")){
                if(ImGui::BeginMenu("Save")){command=SaveItems();ImGui::EndMenu();}
                if(ImGui::MenuItem("Load...","Ctrl+O"))command=Command::Load;
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Edit")){
                if(ImGui::MenuItem("Vertex list",nullptr,vertexListOpen))ToggleVertexList(editor);
                if(ImGui::MenuItem("General",nullptr,generalOpen)){FinishEdit(editor);generalOpen=!generalOpen;}
                if(ImGui::MenuItem("Triangle material",nullptr,trianglePanelOpen,selection.triangle>=0)){FinishEdit(editor);trianglePanelOpen=!trianglePanelOpen;}
                ImGui::Separator();
                if(ImGui::MenuItem("Undo","Ctrl+Z"))command=Command::Undo;
                if(ImGui::MenuItem("Redo","Ctrl+Y"))command=Command::Redo;
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("View")){
                if(ImGui::MenuItem("Frame shape"))Frame(mesh.custom);
                ImGui::MenuItem("Camera",nullptr,&cameraMode);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        if(ImGui::Button("Save"))ImGui::OpenPopup("Save destination");
        if(ImGui::BeginPopup("Save destination")){command=SaveItems();ImGui::EndPopup();}
        ImGui::SameLine();if(ImGui::Button("Load"))command=Command::Load;
        ImGui::SetItemTooltip("Load a shape into the editing copy (Ctrl+O).");
        ImGui::SameLine();if(ImGui::Button("General")){FinishEdit(editor);generalOpen=!generalOpen;}
        ImGui::SameLine();ImGui::TextDisabled("|");ImGui::SameLine();
        if(ImGui::Button("Frame shape"))Frame(mesh.custom);
        ImGui::SameLine();ImGui::Checkbox("Camera",&cameraMode);
        ImGui::SameLine();ImGui::TextDisabled("|");ImGui::SameLine();ImGui::TextUnformatted(objectName.c_str());
        auto label=filePath.empty()?std::string("Editing copy (no asset file)"):PathLabel(filePath.filename())+(IsDirty(editor)?" *":"");
        ImGui::TextDisabled("%s",label.c_str());
        if(!filePath.empty())ImGui::SetItemTooltip("%s",PathLabel(filePath).c_str());
        if(!fileError.empty()){
            ImGui::PushStyleColor(ImGuiCol_Text,{1.f,.4f,.35f,1.f});ImGui::TextWrapped("%s",fileError.c_str());ImGui::PopStyleColor();
        }
        ImGui::Separator();return command;
    }
    void Execute(EditorSession& editor,Command command,AssetDatabase* assets=nullptr){
        RICE_BENCHMARK_FUNCTION();
        if(command==Command::None)return;
        try{
            FinishEdit(draft);boxing=false;pointer.Cancel();wheel.Cancel();
            if(command==Command::Undo){draft.Undo();return;}
            if(command==Command::Redo){draft.Redo();return;}
            if(command==Command::InScene){SaveInScene(editor,assets);return;}
            if(command==Command::AsAsset){SaveAsAsset(editor);return;}
            auto suggested=filePath.empty()?defaultDirectory/"shape.riceshape":filePath;
            if(auto chosen=filePicker(command!=Command::Load,suggested)){
                if(command==Command::Load)LoadFile(editor,*chosen);else SaveFile(editor,*chosen);
            }
        }catch(const std::exception& error){fileError=error.what();}
    }
    void DrawList(EditorSession& editor,MeshRenderer& mesh){
        RICE_BENCHMARK_FUNCTION();
        float closeSize=ImGui::GetFrameHeight();float right=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Vertex list");ImGui::SameLine();
        ImGui::SetCursorPosX(right-closeSize);
        bool close=ImGui::Button("X##CloseVertexList",{closeSize,closeSize});ImGui::SetItemTooltip("Close Vertex list");
        if(close){FinishEdit(editor);vertexListOpen=false;return;}
        ImGui::Separator();
        ImGui::Text("%zu selected / %zu vertices",selection.selected.size(),selection.order.size());
        ImGui::TextDisabled("Local coordinates | Shift: range");
        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::GetIO().WantTextInput&&!ImGui::IsAnyItemActive()){
            int direction=ImGui::IsKeyPressed(ImGuiKey_UpArrow)?-1:ImGui::IsKeyPressed(ImGuiKey_DownArrow)?1:0;
            if(direction)selection.Step(direction,ImGui::GetIO().KeyShift);
        }
        bool activeCoordinateSeen=false;
        if(ImGui::BeginTable("Vertices",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY|ImGuiTableFlags_SizingStretchProp,ImGui::GetContentRegionAvail())){
            ImGui::TableSetupColumn("Vertex",ImGuiTableColumnFlags_WidthFixed,88);
            ImGui::TableSetupColumn("X");ImGui::TableSetupColumn("Y");ImGui::TableSetupColumn("Z");
            ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
            ImGuiListClipper clipper;clipper.Begin(static_cast<int>(selection.order.size()));
            // Keep the focused row available after a keyboard range selection.
            if(selection.active>=0)clipper.IncludeItemByIndex(selection.active);
            if(selection.cursor>=0)clipper.IncludeItemByIndex(selection.cursor);
            while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row){
                auto vertex=selection.order[row];ImGui::PushID(row);ImGui::TableNextRow();ImGui::TableNextColumn();
                auto label="vertex "+std::to_string(row);
                bool triangleVertex=selection.triangle>=0&&selection.selected.contains(vertex);
                if(triangleVertex)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.4f,.8f,1.f,1));
                if(ImGui::Selectable(label.c_str(),selection.selected.contains(vertex))){
                    FinishEdit(editor);selection.Click(row,ImGui::GetIO().KeyShift,true);
                }
                if(triangleVertex)ImGui::PopStyleColor();
                if(selection.cursor==row&&(ImGui::IsKeyPressed(ImGuiKey_UpArrow)||ImGui::IsKeyPressed(ImGuiKey_DownArrow)))ImGui::SetScrollHereY();
                for(int axis=0;axis<3;++axis){
                    ImGui::TableNextColumn();ImGui::PushID(axis);ImGui::SetNextItemWidth(-1);
                    if(triangleVertex)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.4f,.8f,1.f,1));
                    auto localPivot=Pivot();float value=mesh.custom.positions[vertex][axis]-localPivot[axis];
                    bool changed=ImGui::DragFloat("##coordinate",&value,.01f,0,0,"%.3f");
                    auto item=ImGui::GetItemID();
                    if(ImGui::IsItemActivated()){
                        FinishEdit(editor);selection.Click(row,false);BeginEdit(editor,mesh);coordinateEdit=true;coordinateItem=item;
                    }
                    if(ImGui::IsItemActive())activeCoordinateSeen=true;
                    if(changed&&std::isfinite(value)){
                        if(!coordinateEdit){selection.Click(row,false);BeginEdit(editor,mesh);coordinateEdit=true;coordinateItem=item;}
                        mesh.custom.positions[vertex][axis]=value+localPivot[axis];mesh.custom.Touch();camera.position+=localPivot-Pivot();++editor.previewRevision;
                    }
                    if(coordinateEdit&&coordinateItem==item&&ImGui::IsItemDeactivated())FinishEdit(editor);
                    if(triangleVertex)ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if(coordinateEdit&&!activeCoordinateSeen)FinishEdit(editor);
    }
    void DrawTriangleMaterial(EditorSession& editor,MeshRenderer& mesh,AssetDatabase* assets){
        float closeSize=ImGui::GetFrameHeight(),right=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();ImGui::Text("Triangle %d",selection.triangle);ImGui::SameLine();ImGui::SetCursorPosX(right-closeSize);
        if(ImGui::Button("X##CloseTriangle",{closeSize,closeSize})){FinishEdit(editor);trianglePanelOpen=false;return;}
        ImGui::SeparatorText("Material");
        auto base=MaterialSettings(DraftObject().GetComponent<Material>());if(!DraftObject().GetComponent<Material>())base.enabled=true;
        auto value=mesh.triangleMaterials.At(static_cast<uint32_t>(selection.triangle),base);
        if(DrawMaterialFields(value,assets)){
            if(!surfaceEdit){FinishEdit(editor);BeginEdit(editor,mesh);surfaceEdit=true;}
            mesh.triangleMaterials.faces[static_cast<uint32_t>(selection.triangle)]=std::move(value);++editor.previewRevision;
        }
        if(surfaceEdit&&!ImGui::IsAnyItemActive())FinishEdit(editor);
        ImGui::TextWrapped("Save > in scene applies these settings to this object.");
    }
    void DrawGeneral(EditorSession& editor,EditorSession& source){
        float closeSize=ImGui::GetFrameHeight(),right=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("General");ImGui::SameLine();ImGui::SetCursorPosX(right-closeSize);
        if(ImGui::Button("X##CloseGeneral",{closeSize,closeSize})){FinishEdit(editor);generalOpen=false;return;}
        ImGui::Separator();
        auto oldPivot=Pivot();bool changed=false;
        BeforeEditorEdit beforeEdit=[&]{
            if(!generalEdit){FinishEdit(editor);BeginEdit(editor,*Mesh(editor));generalEdit=true;}
            changed=true;
        };
        DrawPivotFields(DraftObject(),beforeEdit);
        ImGui::TextDisabled("0: left / bottom / back   1: opposite");
        if(auto* anchor=DraftObject().GetComponent<Anchor>()){
            if(ImGui::CollapsingHeader("Anchor",ImGuiTreeNodeFlags_DefaultOpen)){
                DrawAnchorFields(*anchor,*source.scene,target,true,beforeEdit);
                if(ImGui::Button("Remove Anchor")){beforeEdit();DraftObject().RemoveComponent<Anchor>();DraftObject().FlushComponents();}
            }
        }else if(ImGui::Button("Add Anchor")){beforeEdit();DraftObject().AddComponent<Anchor>();}
        ImGui::PushID("CameraFollower");
        if(auto* f=DraftObject().GetComponent<CameraFollower>()){
            if(ImGui::CollapsingHeader("Camera Follower",ImGuiTreeNodeFlags_DefaultOpen)){DrawCameraFollowerFields(*f,*source.scene,target,beforeEdit);if(ImGui::Button("Remove Camera Follower")){beforeEdit();DraftObject().RemoveComponent<CameraFollower>();DraftObject().FlushComponents();}}
        }else if(ImGui::Button("Add Camera Follower")){beforeEdit();DraftObject().AddComponent<CameraFollower>();}
        ImGui::PopID();
        if(changed){
            camera.position+=oldPivot-Pivot();++editor.previewRevision;
        }
        if(generalEdit&&!ImGui::IsAnyItemActive())FinishEdit(editor);
    }
    void DrawCanvas(EditorSession& editor,MeshRenderer& mesh,Material* material){
        RICE_BENCHMARK_FUNCTION();
        auto available=ImGui::GetContentRegionAvail();if(available.x<1||available.y<1)return;
        Vector2 size(available.x,available.y),origin(ImGui::GetCursorScreenPos().x,ImGui::GetCursorScreenPos().y);
        canvasOrigin=origin;canvasSize=size;
        ImGui::InvisibleButton("ShapeCanvas",available,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight|ImGuiButtonFlags_MouseButtonMiddle);
        bool hovered=ImGui::IsItemHovered();auto& io=ImGui::GetIO();Vector2 mouse(io.MousePos.x,io.MousePos.y);
        bool blocked=io.WantTextInput||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
        if(!focused||blocked){if(dragButton>=0)FinishEdit(editor);boxing=false;pointer.Cancel();wheel.Cancel();}
        UpdateDrag(editor,mesh,mouse);
        auto offset=Pivot();auto [low,high]=mesh.custom.Bounds();low-=offset;high-=offset;
        auto wheelResult=wheel.Update(mouse,ImGui::IsMouseClicked(2),ImGui::IsMouseDown(2),hovered&&!blocked&&dragButton<0&&!boxing,!focused||blocked||ImGui::IsKeyPressed(ImGuiKey_Escape));
        if(wheelResult.toggle)cameraMode=!cameraMode;
        if(glm::length(wheelResult.orbitDelta)>0)camera.Orbit((low+high)*.5f,wheelResult.orbitDelta);
        if(hovered&&!blocked&&dragButton<0&&!boxing&&!wheelResult.captured&&io.MouseWheel!=0)camera.Zoom(io.MouseWheel*std::max(.1f,glm::length(camera.position-(low+high)*.5f)*.1f));
        if(cameraMode&&ImGui::IsWindowFocused()&&!blocked&&dragButton<0&&!boxing){
            Vector3 movement(float(ImGui::IsKeyDown(ImGuiKey_D))-float(ImGui::IsKeyDown(ImGuiKey_A)),float(ImGui::IsKeyDown(ImGuiKey_E))-float(ImGui::IsKeyDown(ImGuiKey_Q)),float(ImGui::IsKeyDown(ImGuiKey_W))-float(ImGui::IsKeyDown(ImGuiKey_S)));
            if(!io.KeyCtrl&&!io.KeyShift)camera.Move(movement,io.DeltaTime);
        }
        auto vp=camera.Get(size.x,size.y).GetVPMatrix();
        if(spatialRevision!=mesh.custom.revision){spatial.Rebuild(mesh.custom);spatialRevision=mesh.custom.revision;}
        auto projected=ShapeEditorMath::ProjectVisible(mesh.custom,selection,spatial,camera.position,origin,size,vp,offset);
        auto markers=ShapeEditorMath::TriangleMarkers(mesh.custom,spatial,camera.position,origin,size,vp,offset);
        int hotTriangle=-1;
        if(hovered&&!blocked)for(const auto& marker:markers){auto hit=marker.triangle==hoveredTriangle?ShapeEditorMath::ScaleMarker(marker,1.2f):marker;if(ShapeEditorMath::HitTriangle(hit,mouse))hotTriangle=marker.triangle;}
        hoveredTriangle=hotTriangle;
        int hot=-1;float nearest=ShapeEditorMath::HitRadius;
        if(hovered&&!blocked)for(size_t row=0;row<projected.size();++row)if(projected[row]){
            float distance=glm::length(Vector2(*projected[row])-mouse);if(distance<nearest){nearest=distance;hot=static_cast<int>(row);}
        }
        ShapeEditorMath::Cube cube;
        if(selection.ActiveVertex()>=0)cube=ShapeEditorMath::MakeCube(mesh.custom.positions[selection.ActiveVertex()]-offset,camera.position,origin,size,vp);
        int face=hovered&&!blocked?ShapeEditorMath::HitFace(cube,mouse):-1;
        auto* anchor=DraftObject().GetComponent<Anchor>();ShapeEditorMath::Cube anchorCube;
        if(anchor&&anchor->enabled&&anchor->showInEditor)anchorCube=ShapeEditorMath::MakeCube(anchor->transform.position,camera.position,origin,size,vp);
        int anchorFace=hovered&&!blocked?ShapeEditorMath::HitFace(anchorCube,mouse):-1;
        auto* follower=DraftObject().GetComponent<CameraFollower>();
        if(!follower||!follower->showInEditor)followerSelected=false;
        auto followerAxes=followerSelected?ShapeEditorMath::TranslationAxes(follower->transform.position,origin,size,vp):std::vector<ShapeEditorMath::TranslationAxis>{};
        int followerAxis=hovered&&!blocked?ShapeEditorMath::HitTranslationAxis(followerAxes,mouse):-1;
        bool followerHot=follower&&follower->showInEditor&&hovered&&!blocked&&ShapeEditorMath::HitCameraIcon(follower->transform.GetMatrix(),mouse,origin,size,vp);
        bool consumed=dragButton>=0||boxing||wheelResult.captured||coordinateEdit;
        if(!blocked&&hovered&&!consumed&&!pointer.Active()){
            if(followerAxis>=0&&ImGui::IsMouseClicked(0)){
                BeginEdit(editor,mesh);dragFollower=true;dragButton=0;dragAxis=followerAxis;dragStart=lastDragMouse=mouse;
                for(const auto& axis:followerAxes)if(axis.axis==dragAxis){dragDirection=glm::normalize(axis.end-axis.start);unitsPerPixel=axis.unitsPerPixel;}
                consumed=true;
            }
            else if(followerHot&&ImGui::IsMouseClicked(0)){followerSelected=true;selection.selected.clear();selection.active=selection.anchor=selection.cursor=selection.triangle=-1;consumed=true;}
            else if(io.KeyCtrl&&ImGui::IsMouseClicked(0)){boxing=true;boxStart=mouse;boxBase=io.KeyShift?selection.selected:std::set<uint32_t>{};consumed=true;}
            else if(io.KeyShift&&ImGui::IsMouseClicked(0)&&hot>=0){selection.Click(hot,true);consumed=true;}
            else if(anchorFace>=0&&(ImGui::IsMouseClicked(0)||ImGui::IsMouseClicked(1))){StartDrag(editor,mesh,anchorCube.faces[anchorFace],ImGui::IsMouseClicked(0)?0:1,mouse,origin,size,vp,true);consumed=true;}
            else if(face>=0&&(ImGui::IsMouseClicked(0)||ImGui::IsMouseClicked(1))){StartDrag(editor,mesh,cube.faces[face],ImGui::IsMouseClicked(0)?0:1,mouse,origin,size,vp);consumed=true;}
            else if(hot>=0&&ImGui::IsMouseClicked(0)){selection.Click(hot,io.KeyShift);consumed=true;}
            else if(hotTriangle>=0&&ImGui::IsMouseClicked(0)){FinishEdit(editor);selection.SelectTriangle(mesh.custom,hotTriangle);trianglePanelOpen=vertexListOpen=true;consumed=true;}
            else if(!cameraMode&&ImGui::IsMouseClicked(0)){selection.selected.clear();selection.active=selection.anchor=selection.cursor=selection.triangle=-1;consumed=true;}
        }
        if(!dragFollower&&!followerHot&&followerAxis<0&&hovered&&!blocked&&ImGui::IsMouseClicked(0))followerSelected=false;
        if(boxing){
            if(ImGui::IsKeyPressed(ImGuiKey_Escape)){selection.selected=boxBase;boxing=false;}
            else{selection.Box(projected,boxStart,mouse,boxBase);if(!ImGui::IsMouseDown(0))boxing=false;}
        }
        int pressed=ImGui::IsMouseClicked(1)?1:cameraMode&&ImGui::IsMouseClicked(0)?0:-1;
        auto nav=pointer.Update(mouse,pressed,ImGui::IsMouseDown(0),ImGui::IsMouseDown(1),hovered&&!blocked&&!consumed,blocked||!focused||consumed||ImGui::IsKeyPressed(ImGuiKey_Escape));
        if(glm::length(nav.delta)>0){if(nav.button==1)camera.Look(nav.delta);else camera.Pan(nav.delta,size.y);vp=camera.Get(size.x,size.y).GetVPMatrix();projected=ShapeEditorMath::ProjectVisible(mesh.custom,selection,spatial,camera.position,origin,size,vp,offset);}
        if(!renderer)renderer=CreateScope<SceneRenderer>();
        auto texture=viewport.Render(static_cast<int>(size.x),static_cast<int>(size.y),[&]{renderer->RenderShape(mesh,camera.Get(size.x,size.y),material,offset);});
        auto* draw=ImGui::GetWindowDrawList();ImGui::PushClipRect(Point(origin),Point(origin+size),true);
        draw->AddImage(static_cast<ImTextureID>(texture),Point(origin),Point(origin+size),{0,1},{1,0});
        // Reproject after navigation; marker size is measured in screen pixels.
        markers=ShapeEditorMath::TriangleMarkers(mesh.custom,spatial,camera.position,origin,size,vp,offset);
        for(auto marker:markers){if(marker.triangle==hotTriangle)marker=ShapeEditorMath::ScaleMarker(marker,1.2f);
            draw->AddTriangleFilled(Point(marker.points[0]),Point(marker.points[1]),Point(marker.points[2]),IM_COL32_WHITE);
            if(marker.triangle==selection.triangle)draw->AddTriangle(Point(marker.points[0]),Point(marker.points[1]),Point(marker.points[2]),IM_COL32(102,204,255,255),2.f);
        }
        for(size_t row=0;row<projected.size();++row)if(projected[row]&&static_cast<int>(row)!=selection.active){
            bool near=hovered&&glm::length(mouse-Vector2(*projected[row]))<=ShapeEditorMath::HitRadius;
            auto color=selection.selected.contains(selection.order[row])?(selection.triangle>=0?IM_COL32(102,204,255,255):IM_COL32(255,220,35,255)):IM_COL32(255,255,255,255);
            draw->AddCircleFilled(Point(Vector2(*projected[row])),near?ShapeEditorMath::HoverRadius:ShapeEditorMath::PointRadius,color,16);
        }
        if(selection.ActiveVertex()>=0){
            cube=ShapeEditorMath::MakeCube(mesh.custom.positions[selection.ActiveVertex()]-offset,camera.position,origin,size,vp);
            const ImU32 colors[]={IM_COL32(240,65,65,255),IM_COL32(55,205,90,255),IM_COL32(65,115,245,255)};
            int hotFace=hovered?ShapeEditorMath::HitFace(cube,mouse):-1;
            for(size_t i=0;i<cube.faces.size();++i){auto& f=cube.faces[i];
                draw->AddQuadFilled(Point(f.points[0]),Point(f.points[1]),Point(f.points[2]),Point(f.points[3]),colors[f.axis]);
                draw->AddQuad(Point(f.points[0]),Point(f.points[1]),Point(f.points[2]),Point(f.points[3]),static_cast<int>(i)==hotFace?IM_COL32(255,255,255,255):IM_COL32(35,35,40,255),1.f);
            }
        }
        if(anchor&&anchor->enabled&&anchor->showInEditor){
            anchorCube=ShapeEditorMath::MakeCube(anchor->transform.position,camera.position,origin,size,vp);
            const ImU32 colors[]={IM_COL32(240,65,65,255),IM_COL32(55,205,90,255),IM_COL32(65,115,245,255)};
            for(const auto& f:anchorCube.faces){draw->AddQuadFilled(Point(f.points[0]),Point(f.points[1]),Point(f.points[2]),Point(f.points[3]),colors[f.axis]);draw->AddQuad(Point(f.points[0]),Point(f.points[1]),Point(f.points[2]),Point(f.points[3]),IM_COL32(255,220,35,255),1.f);}
            DrawAnchorArrow(draw,anchor->transform.position,Quaternion(glm::radians(anchor->transform.rotation))*Vector3(0,0,1),origin,size,vp);
        }
        if(auto* f=DraftObject().GetComponent<CameraFollower>();f&&f->showInEditor)DrawCameraIcon(draw,f->transform.GetMatrix(),origin,size,vp,f->enabled,followerSelected);
        if(followerSelected&&follower){
            const ImU32 colors[]={IM_COL32(240,65,65,255),IM_COL32(55,205,90,255),IM_COL32(65,115,245,255)};
            for(const auto& axis:ShapeEditorMath::TranslationAxes(follower->transform.position,origin,size,vp)){
                bool active=dragFollower?dragAxis==axis.axis:followerAxis==axis.axis;
                auto color=active?IM_COL32(255,220,35,255):colors[axis.axis];
                auto direction=glm::normalize(axis.end-axis.start);Vector2 side(-direction.y,direction.x);
                draw->AddLine(Point(axis.start),Point(axis.end),color,active?3.f:2.f);
                draw->AddTriangleFilled(Point(axis.end),Point(axis.end-direction*10.f+side*5.f),Point(axis.end-direction*10.f-side*5.f),color);
                draw->AddText(Point(axis.end+side*8.f),color,axis.axis==0?"X":axis.axis==1?"Y":"Z");
            }
            if(followerAxis>=0&&!dragFollower)ImGui::SetTooltip("Drag to move Camera Follower along pivot %s",followerAxis==0?"X":followerAxis==1?"Y":"Z");
        }
        if(auto zero=GizmoMath::Project(Vector3(0),origin,size,vp)){
            draw->AddLine({zero->x-4,zero->y},{zero->x+4,zero->y},IM_COL32(170,170,180,200));draw->AddLine({zero->x,zero->y-4},{zero->x,zero->y+4},IM_COL32(170,170,180,200));
        }
        if(boxing){Vector2 a(boxStart.x,mouse.y),b(mouse.x,boxStart.y);DashedLine(draw,boxStart,b);DashedLine(draw,b,mouse);DashedLine(draw,mouse,a);DashedLine(draw,a,boxStart);}
        ImGui::PopClipRect();
    }
public:
    explicit ShapeEditor(FilePicker picker=PickShapeFile):filePicker(std::move(picker)){}
    bool IsOpen()const{return open;}
    bool Focused()const{return open&&focused;}
    bool Editing()const{return dragButton>=0||coordinateEdit||generalEdit||surfaceEdit;}
    const ShapeSelection& Selection()const{return selection;}
    const EditorCamera& Camera()const{return camera;}
    Vector2 CanvasOrigin()const{return canvasOrigin;}
    Vector2 CanvasSize()const{return canvasSize;}
    bool VertexListOpen()const{return vertexListOpen;}
    bool FollowerSelected()const{return followerSelected;}
    bool GeneralOpen()const{return generalOpen;}
    bool TrianglePanelOpen()const{return trianglePanelOpen&&selection.triangle>=0;}
    EditorSession& Draft(){return draft;}
    GameObject& DraftObject(){return *draft.scene->Find(target);}
    Vector3 LocalPivot()const{return Pivot();}
    const fs::path& FilePath()const{return filePath;}
    const std::string& FileError()const{return fileError;}
    bool IsDirty(EditorSession&)const{
        return open&&(filePath.empty()||ShapeFile::Encode(CaptureDraft())!=savedData||MaterialsData()!=appliedMaterials);
    }
    void SaveFile(EditorSession& editor,const fs::path& path){
        RICE_BENCHMARK_FUNCTION();
        if(!Source(editor))throw std::runtime_error("No editable Custom shape is open");
        if(path.empty())throw std::runtime_error("Choose a shape filename");
        FinishEdit(draft);auto destination=fs::absolute(path);if(!destination.has_extension())destination+=".riceshape";
        auto data=CaptureDraft();ShapeFile::Save(destination,data);
        filePath=std::move(destination);savedData=ShapeFile::Encode(data);fileError.clear();
    }
    void SaveAsAsset(EditorSession& editor){
        if(filePath.empty())throw std::runtime_error("No existing asset file. Use Save > elsewhere first.");
        if(!fs::is_regular_file(filePath))throw std::runtime_error("The existing asset file is missing. Use Save > elsewhere.");
        SaveFile(editor,filePath);
    }
    fs::path SaveInScene(EditorSession& editor,AssetDatabase* assets=nullptr){
        RICE_BENCHMARK_FUNCTION();
        auto* source=Source(editor);if(!source)throw std::runtime_error("No editable Custom shape is open");
        FinishEdit(draft);
        auto stem=filePath.empty()?fs::path(std::u8string(source->name.begin(),source->name.end())).wstring():filePath.stem().wstring();
        for(auto& c:stem)if(c<32||std::wstring_view(L"<>:\"/\\|?*").find(c)!=std::wstring_view::npos)c=L'_';
        while(!stem.empty()&&(stem.back()==L'.'||stem.back()==L' '))stem.pop_back();if(stem.empty())stem=L"shape";
        auto suffix=stem.rfind(L"_scene");bool sceneSuffix=suffix!=std::wstring::npos;
        if(sceneSuffix){auto tail=stem.substr(suffix+6);sceneSuffix=tail.empty()||(tail.size()>1&&tail.front()==L'_'&&tail.find_first_not_of(L"0123456789",1)==std::wstring::npos);}
        int copy=sceneSuffix?1:0;if(sceneSuffix)stem.resize(suffix);
        fs::path destination;
        do{auto name=stem+L"_scene"+(copy?L"_"+std::to_wstring(copy):L"")+L".riceshape";destination=fs::absolute(defaultDirectory/"Shapes"/name);++copy;}while(fs::exists(destination));
        auto data=CaptureDraft();auto before=editor.CaptureObject(target);ShapeFile::Save(destination,data);
        if(assets)assets->Refresh();
        data.Apply(*source);
        if(data.triangleMaterials.Edited()&&!source->GetComponent<Material>())source->AddComponent<Material>().enabled=false;
        if(auto* anchor=source->GetComponent<Anchor>()){
            auto* draftAnchor=DraftObject().GetComponent<Anchor>();auto* child=editor.scene->Find(draftAnchor?draftAnchor->child:0);
            anchor->child=child&&child->GetParent()==source?child->GetID():0;
        }
        source->GetComponent<MeshRenderer>()->shapeAsset=PathLabel(fs::relative(destination,defaultDirectory));
        ++editor.previewRevision;editor.Record(before);
        filePath=destination;savedData=ShapeFile::Encode(data);appliedMaterials=MaterialsData();fileError.clear();return destination;
    }
    void LoadFile(EditorSession& editor,const fs::path& path){
        RICE_BENCHMARK_FUNCTION();
        if(!Source(editor))throw std::runtime_error("No editable Custom shape is open");
        auto source=fs::absolute(path);auto loaded=ShapeFile::LoadDocument(source);
        FinishEdit(draft);auto before=draft.CaptureObject(target);loaded.Apply(DraftObject());++draft.previewRevision;draft.Record(before);
        auto& mesh=DraftObject().GetComponent<MeshRenderer>()->custom;selection.Reset(mesh);spatialRevision=0;Frame(mesh);
        boxing=false;pointer.Cancel();wheel.Cancel();
        filePath=std::move(source);savedData=ShapeFile::Encode(loaded);fileError.clear();
    }
    void Commit(EditorSession&){FinishEdit(draft);}
    void SyncMaterialOverwrite(EditorSession& editor,EntityID id){
        if(id!=target||!Source(editor))return;
        FinishEdit(draft);auto* source=editor.scene->Find(id);auto* material=source->GetComponent<Material>();
        if(material)ApplyMaterialSettings(DraftObject().AddComponent<Material>(),MaterialSettings(material));
        else{DraftObject().RemoveComponent<Material>();DraftObject().FlushComponents();}
        DraftObject().GetComponent<MeshRenderer>()->triangleMaterials.faces.clear();appliedMaterials=MaterialsData();
    }
    void Open(EditorSession& editor,EntityID id,const fs::path& directory={}){
        RICE_BENCHMARK_FUNCTION();
        Close(editor);target=id;document=editor.documentRevision;
        auto* mesh=Mesh(editor);if(!mesh||editor.playing)return;
        auto* source=editor.scene->Find(id);auto next=CreateScope<Scene>();auto& object=next->CreateGameObject(source->name,id);
        auto& preview=object.AddComponent<MeshRenderer>();preview.shape=int(BasicShape::Custom);preview.color=mesh->color;
        auto data=ShapeDocument::Capture(*source);if(data.mesh.Empty())data.mesh=ShapeMesh::FromPrimitive(BasicShape::Cube);data.Apply(object);
        if(auto* material=source->GetComponent<Material>())ApplyMaterialSettings(object.AddComponent<Material>(),MaterialSettings(material));
        if(auto* anchor=source->GetComponent<Anchor>())object.GetComponent<Anchor>()->child=anchor->child;
        draft.Replace(std::move(next));selection.Reset(preview.custom);Frame(preview.custom);spatialRevision=0;sceneInstance=draft.scene->GetInstanceID();historyRevision=draft.historyRevision;open=focusNext=true;cameraMode=false;
        vertexListOpen=generalOpen=followerSelected=trianglePanelOpen=false;hoveredTriangle=-1;filePath.clear();fileError.clear();savedData=ShapeFile::Encode(data);appliedMaterials=MaterialsData();
        defaultDirectory=directory.empty()?fs::current_path():directory;
        if(!mesh->shapeAsset.empty())filePath=fs::absolute(defaultDirectory/fs::path(std::u8string(mesh->shapeAsset.begin(),mesh->shapeAsset.end())));
    }
    void Close(EditorSession&){
        RICE_BENCHMARK_FUNCTION();
        FinishEdit(draft);open=focused=boxing=false;pointer.Cancel();wheel.Cancel();
    }
    void Draw(EditorSession& source,AssetDatabase* assets=nullptr){
        RICE_BENCHMARK_FUNCTION();
        if(!open)return;
        if(!Source(source)){Close(source);return;}
        auto& editor=draft;auto* mesh=Mesh(editor);if(!mesh){Close(source);return;}
        if(sceneInstance!=editor.scene->GetInstanceID()||historyRevision!=editor.historyRevision){
            camera.position+=cameraPivot-Pivot();cameraPivot=Pivot();
            snapshot.reset();startPositions.clear();dragButton=-1;coordinateEdit=generalEdit=surfaceEdit=boxing=false;pointer.Cancel();wheel.Cancel();
            sceneInstance=editor.scene->GetInstanceID();historyRevision=editor.historyRevision;spatialRevision=0;
            if(selection.order!=mesh->custom.VertexOrder())selection.Reset(mesh->custom);
            if(selection.triangle>=0&&size_t(selection.triangle)>=mesh->custom.indices.size()/3)selection.triangle=-1;
        }
        if(assets){
            auto load=[&](auto& value){if(value.albedoAsset==value.loadedAsset)return;value.loadedAsset=value.albedoAsset;value.albedoTexture.reset();try{if(!value.albedoAsset.empty())value.albedoTexture=assets->Texture(value.albedoAsset);}catch(const std::exception& error){fileError=error.what();}};
            if(auto* material=DraftObject().GetComponent<Material>())load(*material);
            for(auto& [index,value]:mesh->triangleMaterials.faces)load(value);
        }
        ImGui::SetNextWindowSize({1000,650},ImGuiCond_FirstUseEver);
        if(focusNext){ImGui::SetNextWindowFocus();focusNext=false;}
        Command command=Command::None;
        bool shown=ImGui::Begin("Shape Editor",&open,ImGuiWindowFlags_MenuBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if(shown){
            auto* object=source.scene->Find(target);
            command=DrawToolbar(editor,*mesh,object->name);
            ImGui::TextDisabled("Wheel: zoom | MMB drag: orbit | Camera: WASD / QE, L-drag pan, R-drag look");
            ImGui::TextDisabled("Shift-click: select | Ctrl-drag: box | Cube: L-drag move, R-drag rotate | X red / Y green / Z blue");
            float listWidth=std::clamp(ImGui::GetContentRegionAvail().x*.38f,300.f,430.f);
            // Draw the list first so clicking/editing a row reveals its cube in this frame.
            bool trianglePanel=TrianglePanelOpen();
            if((vertexListOpen||generalOpen||trianglePanel)&&ImGui::BeginTable("Shape layout",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_SizingStretchProp)){
                ImGui::TableSetupColumn("View",ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Vertex list",ImGuiTableColumnFlags_WidthFixed,listWidth);
                ImGui::TableNextRow();ImGui::TableSetColumnIndex(1);
                if(trianglePanel){float height=(vertexListOpen||generalOpen)?ImGui::GetContentRegionAvail().y*.45f:0.f;ImGui::BeginChild("Triangle panel",{0,height});DrawTriangleMaterial(editor,*mesh,assets);ImGui::EndChild();}
                if(generalOpen){float height=vertexListOpen?ImGui::GetContentRegionAvail().y*.55f:0.f;ImGui::BeginChild("General panel",{0,height});DrawGeneral(editor,source);ImGui::EndChild();}
                if(vertexListOpen){ImGui::BeginChild("Vertex panel",{0,0});DrawList(editor,*mesh);ImGui::EndChild();}
                ImGui::TableSetColumnIndex(0);ImGui::BeginChild("Shape view",{0,0},false,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
                DrawCanvas(editor,*mesh,DraftObject().GetComponent<Material>());ImGui::EndChild();ImGui::EndTable();
            }else if(!vertexListOpen&&!generalOpen&&!trianglePanel){
                ImGui::BeginChild("Shape view",{0,0},false,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
                DrawCanvas(editor,*mesh,DraftObject().GetComponent<Material>());ImGui::EndChild();
            }
            if(focused&&ImGui::IsKeyPressed(ImGuiKey_Escape)&&coordinateEdit)FinishEdit(editor,true);
        }else{FinishEdit(editor);boxing=false;pointer.Cancel();wheel.Cancel();}
        ImGui::End();
        if(!open)Close(editor);
        if(open&&focused&&!Editing()&&!boxing&&!ImGui::GetIO().WantTextInput&&ImGui::GetIO().KeyCtrl&&
           !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)){
            if(ImGui::GetIO().KeyShift&&ImGui::IsKeyPressed(ImGuiKey_S,false))command=Command::AsAsset;
            else if(ImGui::IsKeyPressed(ImGuiKey_O,false))command=ImGui::GetIO().KeyShift?Command::InScene:Command::Load;
            else if(ImGui::IsKeyPressed(ImGuiKey_Z,false))command=ImGui::GetIO().KeyShift?Command::Redo:Command::Undo;
            else if(ImGui::IsKeyPressed(ImGuiKey_Y,false))command=Command::Redo;
        }
        // Loading and undo may replace geometry/scene storage. File dialogs can
        // also pump native events, so execute only after the window is complete.
        if(open){cameraPivot=Pivot();Execute(source,command,assets);}
    }
};
}
