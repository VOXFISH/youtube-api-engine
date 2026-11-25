#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Editor/ShapeEditor.h"
#include "Editor/EditorClipboard.h"
#include "Project/Prefab.h"
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>
#include <fstream>
#include <iostream>

using namespace Rice;
static void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static bool Near(Vector3 a,Vector3 b,float tolerance=1e-4f){return glm::length(a-b)<tolerance;}
static void DataTests(){
    ComponentRegistry registry;EditorSession editor(registry);
    auto& object=editor.scene->CreateGameObject("Custom test");auto id=object.GetID();
    auto& mesh=object.AddComponent<MeshRenderer>();mesh.SetShape(int(BasicShape::Custom));
    Check(mesh.custom.positions.size()==8&&mesh.custom.indices.size()==36,"Cube has eight welded vertices and twelve triangles");
    for(auto shape:{BasicShape::Cube,BasicShape::Circle,BasicShape::Rectangle,BasicShape::Sphere,BasicShape::Cylinder,BasicShape::Cone}){
        auto geometry=ShapeMesh::FromPrimitive(shape);geometry.Validate();auto [low,high]=geometry.Bounds();
        Check(Near(low,shape==BasicShape::Circle||shape==BasicShape::Rectangle?Vector3(-.5f,-.5f,0):Vector3(-.5f)),"Conversion retains primitive lower bounds");
        Check(Near(high,shape==BasicShape::Circle||shape==BasicShape::Rectangle?Vector3(.5f,.5f,0):Vector3(.5f)),"Conversion retains primitive upper bounds");
        Check(geometry.VertexOrder().size()==geometry.positions.size(),"Each editable vertex is indexed exactly once in the list");
    }
    ShapeSelection selection;selection.Reset(mesh.custom);selection.Click(0,false);selection.Click(2,true);
    Check(selection.selected.size()==2&&selection.ActiveVertex()==int(selection.order[2]),"Shift click adds vertices and changes pivot");
    selection.Click(0,false);Check(selection.selected.size()==2,"Clicking a yellow vertex preserves the selected set");
    selection.Click(7,false);Check(selection.selected.size()==1,"Clicking a white vertex clears the previous set");
    selection.Click(2,false);selection.Click(5,true,true);Check(selection.selected.size()==4,"List shift click selects an ordered range");
    selection.Step(1,true);Check(selection.cursor==6&&selection.active==5&&selection.selected.size()==5,"Shift Down extends range without changing the last clicked pivot");
    selection.Step(-1,true);Check(selection.cursor==5&&selection.active==5&&selection.selected.size()==4,"Shift Up contracts range");
    auto start=mesh.custom.positions;auto snapshot=editor.Snapshot();
    ShapeEditorMath::Translate(mesh.custom,start,selection.selected,{2,0,0});editor.Record(snapshot);
    for(size_t i=0;i<start.size();++i)Check(Near(mesh.custom.positions[i],start[i]+(selection.selected.contains(static_cast<uint32_t>(i))?Vector3(2,0,0):Vector3(0))),"Translation moves only the selected vertices");
    auto moved=mesh.custom.positions;editor.Undo();Check(editor.scene->Find(id)->GetComponent<MeshRenderer>()->custom.positions==start,"Mesh edit is undoable");
    editor.Redo();Check(editor.scene->Find(id)->GetComponent<MeshRenderer>()->custom.positions==moved,"Mesh edit is redoable");
    auto& current=*editor.scene->Find(id)->GetComponent<MeshRenderer>();
    ShapeEditorMath::Rotate(current.custom,moved,0,2,90);
    Check(Near(current.custom.positions[0],moved[0]),"Rotation pivot is the last active vertex");
    for(size_t i=0;i<moved.size();++i){auto relative=moved[i]-moved[0];Check(Near(current.custom.positions[i],moved[0]+Vector3(-relative.y,relative.x,relative.z)),"Rotation affects the entire shape around the pivot");}
    auto encoded=editor.Snapshot();auto restored=SceneCodec(registry).Decode(encoded);
    Check(restored->Find(id)->GetComponent<MeshRenderer>()->custom.positions==current.custom.positions,"Custom mesh survives scene serialization");
    auto legacy=encoded;auto begin=legacy.find("\"Custom mesh\"");legacy.erase(begin,legacy.find('\n',begin)-begin+1);
    Check(SceneCodec(registry).Decode(legacy)->Find(id)->GetComponent<MeshRenderer>()->custom.Empty(),"Legacy scenes with no custom data still load");
    EditorClipboard clipboard;clipboard.CopyObject(*editor.scene,id,registry);clipboard.PasteObject(editor,registry);
    auto* clone=editor.scene->Find(editor.selected)->GetComponent<MeshRenderer>();
    Check(clone->custom.positions==current.custom.positions,"Clipboard retains custom geometry");
    clone->custom.positions[0].x+=10;Check(clone->custom.positions!=current.custom.positions,"Copied objects own independent vertices");
    auto prefab=PrefabAsset::Capture(*editor.scene,id,registry);auto decodedPrefab=PrefabAsset::Decode(prefab->Encode(),registry);
    auto spawned=decodedPrefab->Create(*editor.scene,registry,{0,0,0},false);
    Check(editor.scene->Find(spawned.front())->GetComponent<MeshRenderer>()->custom.indices==current.custom.indices,"Prefab retains topology");
    auto snapshots=ComponentSnapshot::CaptureAll(*editor.scene->Find(id),registry);
    for(auto& value:snapshots)if(value.type=="MeshRenderer"){
        std::istringstream in(value.data);std::string enabled,color,shape;std::getline(in,enabled);std::getline(in,color);std::getline(in,shape);value.data=enabled+'\n'+color+'\n'+shape+'\n';
    }
    auto& legacyObject=editor.scene->CreateGameObject("Legacy prefab");ComponentSnapshot::Apply(legacyObject,snapshots,registry);
    Check(legacyObject.GetComponent<MeshRenderer>()->custom.Empty(),"Old prefab component payload remains compatible");
    bool rejected=false;try{ShapeMesh malformed;std::istringstream bad("1 3 0 0 0 0 1 0 0 0 0 0 0 0");malformed.Read(bad);}catch(...){rejected=true;}
    Check(rejected,"Out-of-range indices are rejected before rendering");
}
static void GeometryTests(){
    auto mesh=ShapeMesh::FromPrimitive(BasicShape::Cube);ShapeSelection selection;selection.Reset(mesh);MeshIntersection spatial;spatial.Rebuild(mesh);
    PerspectiveCamera camera(45,1,.1f,1000);camera.SetPosition({0,0,4});camera.LookAtObj({0,0,0});
    auto visible=ShapeEditorMath::ProjectVisible(mesh,selection,spatial,camera.GetPosition(),{20,30},{600,600},camera.GetVPMatrix());
    selection.Box(visible,{20,30},{620,630});Check(selection.selected.size()==4,"Box selection excludes the four hidden rear vertices");
    for(auto i:selection.selected)Check(mesh.positions[i].z>.49f,"Only front vertices are selected");
    auto reversed=selection;reversed.Box(visible,{620,630},{20,30});Check(reversed.selected==selection.selected,"Box works in both drag directions");
    for(float distance:{.101f,1.f,3.f,20.f,100.f})for(Vector3 direction:{Vector3(0,0,1),glm::normalize(Vector3(1,1,1)),glm::normalize(Vector3(-1,.3f,2))}){
        camera.SetPosition(direction*distance);camera.LookAtObj({0,0,0});auto cube=ShapeEditorMath::MakeCube({0,0,0},camera.GetPosition(),{20,30},{600,600},camera.GetVPMatrix());
        Vector2 low(1e30f),high(-1e30f);Check(!cube.faces.empty(),"Gizmo remains visible after orbit and zoom");
        for(const auto& face:cube.faces){Vector2 center(0);for(auto point:face.points){low=glm::min(low,point);high=glm::max(high,point);center+=point*.25f;}Check(ShapeEditorMath::HitFace(cube,center)>=0,"Each visible cube face can be picked");}
        auto extent=high-low;Check(std::abs(std::max(extent.x,extent.y)-20)<.02f,"Cube remains exactly twenty pixels across view rotations and zoom");
    }
    auto ordered=mesh;std::rotate(ordered.indices.begin(),ordered.indices.begin()+9,ordered.indices.end());selection.Reset(ordered);
    Check(selection.order.front()==ordered.indices.front(),"Reopening assigns vertex numbers in index buffer order");
    Scene scene;auto& object=scene.CreateGameObject("Deformed pick");auto& renderer=object.AddComponent<MeshRenderer>();renderer.SetShape(int(BasicShape::Custom));
    for(auto& p:renderer.custom.positions)p.x+=4;renderer.custom.Touch();
    Check(GizmoMath::Pick(scene,{{4,0,5},{0,0,-1}})==object.GetID(),"Main view picks deformed geometry outside primitive bounds");
    Check(!GizmoMath::Pick(scene,{{0,0,5},{0,0,-1}}),"Main view no longer picks the original cube position");
}
static void TriangleDataTests(){
    auto mesh=ShapeMesh::FromPrimitive(BasicShape::Cube);MeshIntersection spatial;spatial.Rebuild(mesh);
    PerspectiveCamera camera(45,1,.1f,1000);camera.SetPosition({0,0,4});camera.LookAtObj({0,0,0});
    auto markers=ShapeEditorMath::TriangleMarkers(mesh,spatial,camera.GetPosition(),{0,0},{600,600},camera.GetVPMatrix());
    Check(markers.size()==2,"Only the two visible front triangles have markers");
    for(const auto& marker:markers){
        std::array<Vector2,3> projected;Vector2 center(0);
        for(int c=0;c<3;++c){projected[c]=Vector2(*GizmoMath::Project(mesh.positions[mesh.indices[marker.triangle*3+c]],{0,0},{600,600},camera.GetVPMatrix()));center+=projected[c]/3.f;}
        for(int c=0;c<3;++c)Check(glm::length(marker.points[c]-(center+(projected[c]-center)*.2f))<.001f,"Markers are twenty percent of the projected face about its centroid");
        auto hover=ShapeEditorMath::ScaleMarker(marker,1.2f);Check(std::abs(glm::length(hover.points[0]-hover.points[1])/glm::length(marker.points[0]-marker.points[1])-1.2f)<.0001f,"Hover enlarges marker lengths by twenty percent");
        Check(ShapeEditorMath::HitTriangle(marker,center)&&!ShapeEditorMath::HitTriangle(marker,center+Vector2(300)),"Only the triangle interior is pickable");
    }
    camera.SetPosition({0,0,100});camera.LookAtObj({0,0,0});Check(ShapeEditorMath::TriangleMarkers(mesh,spatial,camera.GetPosition(),{0,0},{600,600},camera.GetVPMatrix()).empty(),"Markers with maximum screen extent at most five pixels are hidden");
    ComponentRegistry registry;EditorSession editor(registry);auto& object=editor.scene->CreateGameObject("Face materials");auto id=object.GetID();auto& renderer=object.AddComponent<MeshRenderer>();renderer.SetShape(int(BasicShape::Custom));object.AddComponent<Material>();
    SurfaceMaterial first;first.metallicity=.8f;first.roughness=.2f;first.albedoAsset="red-texture";renderer.triangleMaterials.faces[0]=first;
    SurfaceMaterial second;second.roughness=.9f;second.albedoAsset="blue-texture";renderer.triangleMaterials.faces[1]=second;
    auto encoded=editor.Snapshot();auto restored=SceneCodec(registry).Decode(encoded);
    Check(restored->Find(id)->GetComponent<MeshRenderer>()->triangleMaterials.faces==renderer.triangleMaterials.faces,"Scene serialization retains each triangle's material and texture");
    auto old=encoded;auto begin=old.find("\"Triangle materials\"");old.erase(begin,old.find('\n',begin)-begin+1);
    Check(!HasTriangleMaterials(*SceneCodec(registry).Decode(old)->Find(id)),"Older scenes without triangle materials load normally");
    Check(!ShapeFile::DecodeDocument(ShapeFile::Encode(ShapeDocument::Capture(object))).triangleMaterials.Edited(),"Per-object materials do not leak into shared shape assets");
    EditorClipboard clipboard;clipboard.CopyObject(*editor.scene,id,registry);auto copied=clipboard.PasteObject(editor,registry);auto* clone=editor.scene->Find(copied)->GetComponent<MeshRenderer>();
    Check(clone->triangleMaterials.faces==renderer.triangleMaterials.faces,"Copied objects retain independent per-face settings");clone->triangleMaterials.faces[0].roughness=.6f;
    Check(renderer.triangleMaterials.faces[0].roughness==.2f,"Editing a duplicate does not alter the original");
    auto prefab=PrefabAsset::Decode(PrefabAsset::Capture(*editor.scene,id,registry)->Encode(),registry);auto instances=prefab->Create(*editor.scene,registry,{0,0,0},false);
    Check(editor.scene->Find(instances.front())->GetComponent<MeshRenderer>()->triangleMaterials.faces==renderer.triangleMaterials.faces,"Prefab runtime instances restore per-face settings");
    MaterialOverwritePrompt prompt;auto proposed=MaterialSettings(object.GetComponent<Material>());proposed.albedoAsset="uniform-texture";
    auto before=editor.Snapshot();prompt.Queue(*editor.scene,object,proposed);Check(editor.Snapshot()==before,"Inspector proposals cannot mutate materials before Proceed");prompt.Return();Check(editor.Snapshot()==before,"Return preserves every triangle setting");
    prompt.Queue(*editor.scene,object,proposed);Check(prompt.Proceed(*editor.scene,object)&&!HasTriangleMaterials(object)&&object.GetComponent<Material>()->albedoAsset=="uniform-texture","Proceed replaces all per-face settings with the proposed object material");
    Check(HasTriangleMaterials(*editor.scene->Find(copied)),"Inspector overwrite only affects the chosen object");editor.Record(before);editor.Undo();Check(HasTriangleMaterials(*editor.scene->Find(id)),"Undo restores the per-face settings and edited state");
    bool rejected=false;try{TriangleMaterials bad;std::istringstream input("2 0 1 0 .5 \"\" 0 1 0 .5 \"\"");bad.Read(input);}catch(...){rejected=true;}Check(rejected,"Duplicate triangle material entries are rejected");
}

static void FileTests(const fs::path& directory){
    fs::create_directories(directory);
    auto mesh=ShapeMesh::FromPrimitive(BasicShape::Sphere);mesh.positions[3]={.123456789f,-.00003147f,12.3456789f};
    auto text=ShapeFile::Encode(mesh);auto decoded=ShapeFile::Decode(text);
    Check(decoded.positions==mesh.positions&&decoded.indices==mesh.indices&&decoded.uv==mesh.uv,"Shape files retain all float precision, topology and UVs");
    for(const auto& invalid:{std::string("RICE_SHAPE 2\n0 0\n"),std::string("RICE_SCENE 3\n"),std::string("RICE_SHAPE 1\n0 0\n"),text+"trailing",text.substr(0,text.size()/2),std::string("RICE_SHAPE 1\n100001 0\n")}){
        bool rejected=false;try{ShapeFile::Decode(invalid);}catch(...){rejected=true;}Check(rejected,"Malformed, empty, oversized and unsupported shape files are rejected");
    }
    ComponentRegistry registry;EditorSession editor(registry);ShapeEditor panel;
    auto& object=editor.scene->CreateGameObject("File target");auto id=object.GetID();object.transform.position={3,4,5};
    auto& renderer=object.AddComponent<MeshRenderer>();renderer.SetShape(int(BasicShape::Custom));renderer.color={1,.2f,.3f,1};
    auto& other=editor.scene->CreateGameObject("Unchanged object");auto otherID=other.GetID();other.AddComponent<MeshRenderer>().SetShape(int(BasicShape::Custom));
    panel.Open(editor,id,directory);panel.SaveFile(editor,directory/fs::path(L"도형 저장"));auto savedPath=panel.FilePath();
    auto working=[&]() -> ShapeMesh& {return panel.DraftObject().GetComponent<MeshRenderer>()->custom;};auto unchangedScene=editor.Snapshot();
    Check(savedPath.extension()==".riceshape"&&fs::exists(savedPath)&&!panel.IsDirty(editor),"Save adds the extension and handles Unicode paths");
    working().positions[0].x+=1;working().Touch();Check(panel.IsDirty(editor),"Unsaved draft changes are tracked");
    panel.SaveAsAsset(editor);Check(ShapeFile::Load(savedPath).positions==working().positions&&editor.Snapshot()==unchangedScene,"As asset overwrites the existing file without changing the scene");
    auto beforeLoad=working();auto incoming=ShapeMesh::FromPrimitive(BasicShape::Rectangle);
    auto loadPath=directory/"plane.riceshape";ShapeFile::Save(loadPath,incoming);panel.LoadFile(editor,loadPath);
    Check(working().positions==incoming.positions&&working().indices==incoming.indices&&editor.Snapshot()==unchangedScene,"Load replaces only the editing copy");
    Check(renderer.color==Color(1,.2f,.3f,1)&&object.transform.position==Vector3(3,4,5)&&other.GetComponent<MeshRenderer>()->custom.positions.size()==8,"Load preserves materials, transforms and other objects");
    Check(panel.Selection().order.size()==4&&panel.Selection().active==-1&&!panel.IsDirty(editor),"Load resets vertex numbering, selection and the saved state");
    panel.Draft().Undo();Check(working().positions==beforeLoad.positions&&panel.IsDirty(editor),"Load is one draft undo action");
    panel.Draft().Redo();Check(working().positions==incoming.positions&&!panel.IsDirty(editor),"Redo restores loaded geometry");
    auto beforeFailure=panel.Draft().Snapshot();auto badPath=directory/"invalid.riceshape";AtomicWrite(badPath,"RICE_SHAPE 1\n1 3 0 0 0 0 3 0 0 0 0 0 0 0 0\n");
    bool rejected=false;try{panel.LoadFile(editor,badPath);}catch(...){rejected=true;}
    Check(rejected&&panel.Draft().Snapshot()==beforeFailure&&panel.FilePath()==fs::absolute(loadPath),"Failed load preserves the editing copy and current file");
    auto readOnlyPath=directory/"folder.riceshape";fs::create_directories(readOnlyPath);rejected=false;
    try{panel.SaveFile(editor,readOnlyPath);}catch(...){rejected=true;}
    Check(rejected&&panel.FilePath()==fs::absolute(loadPath)&&panel.Draft().Snapshot()==beforeFailure,"Failed save preserves the file association and mesh");
    editor.Select(otherID);panel.LoadFile(editor,savedPath);
    Check(working().positions==beforeLoad.positions&&editor.Snapshot()==unchangedScene,"Loading after scene selection changes still leaves scene objects untouched");
    auto folder=directory/NewAssetID();AssetDatabase assets(folder);panel.Open(editor,id,folder);
    panel.DraftObject().transform.pivot={-1,.25f,2};auto& draftAnchor=panel.DraftObject().AddComponent<Anchor>();draftAnchor.transform.position={1,2,3};
    auto& child=editor.scene->CreateGameObject("Bound child");child.AddComponent<MeshRenderer>();child.SetParent(&object);draftAnchor.child=child.GetID();
    auto sceneBefore=editor.Snapshot();auto first=panel.SaveInScene(editor,&assets);
    Check(first.filename()=="File target_scene.riceshape"&&fs::exists(first),"In scene creates the first named scene asset");
    Check(assets.Entries().size()==1&&assets.Root()/assets.Entries().front().path==first,"In scene registers the new file in the project asset database");
    Check(object.transform.pivot==Vector3(-1,.25f,2)&&object.GetComponent<Anchor>()->child==child.GetID(),"In scene applies pivot and anchor binding");
    auto sceneApplied=editor.Snapshot();working().positions[0].z+=.2f;working().Touch();panel.SaveAsAsset(editor);
    Check(editor.Snapshot()==sceneApplied&&ShapeFile::Load(first).positions==working().positions,"As asset leaves even the currently referenced scene copy unchanged");
    auto second=panel.SaveInScene(editor),third=panel.SaveInScene(editor);
    Check(second.filename()=="File target_scene_1.riceshape"&&third.filename()=="File target_scene_2.riceshape","In scene numbers copies from one without overwriting");
    editor.Undo();editor.Undo();editor.Undo();Check(editor.Snapshot()==sceneBefore,"Scene saves can be undone independently of draft changes and files");
    panel.Close(editor);
}

struct Fixture {
    ComponentRegistry registry;EditorSession editor{registry};EntityID id=0;
    std::optional<fs::path> nextFile;std::vector<bool> pickerModes;
    ShapeEditor panel{[this](bool save,const fs::path&){pickerModes.push_back(save);return std::exchange(nextFile,std::nullopt);}};
    std::vector<ImVec2> rowCenters,coordinateCenters;
    bool externalOpen=false;
    AssetDatabase* assets=nullptr;
    bool materialInspector=false;
    MaterialOverwritePrompt materialPrompt;
    explicit Fixture(const fs::path& directory={}){
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1100,750};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        ImGui::StyleColorsDark();ImGui_ImplOpenGL3_Init("#version 330");
        auto& object=editor.scene->CreateGameObject("Custom cube");id=object.GetID();object.AddComponent<MeshRenderer>().SetShape(int(BasicShape::Custom));panel.Open(editor,id,directory);
        Frame();Frame();
    }
    ~Fixture(){panel.Close(editor);ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();}
    MeshRenderer& Mesh(){return *panel.DraftObject().GetComponent<MeshRenderer>();}
    void Frame(ImVec2 mouse={-100,-100},bool left=false,bool right=false,bool ctrl=false,bool shift=false){
        auto& io=ImGui::GetIO();io.AddMousePosEvent(mouse.x,mouse.y);io.AddMouseButtonEvent(0,left);io.AddMouseButtonEvent(1,right);
        io.AddKeyEvent(ImGuiMod_Ctrl,ctrl);io.AddKeyEvent(ImGuiMod_Shift,shift);
        ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({20,20},ImGuiCond_Always);panel.Draw(editor,assets);
        if(materialInspector){
            auto& object=*editor.scene->Find(id);ImGui::SetNextWindowPos({625,155},ImGuiCond_Always);ImGui::SetNextWindowSize({410,280});
            ImGui::Begin(HasTriangleMaterials(object)?"Material (edited in shape editor)###Test Material Inspector":"Material###Test Material Inspector");
            auto before=editor.Snapshot();if(DrawMaterialInspector(*editor.scene,object,assets,materialPrompt)){editor.Record(before);panel.SyncMaterialOverwrite(editor,id);}ImGui::End();
        }
        if(externalOpen){ImGui::SetNextWindowPos({880,40},ImGuiCond_Always);ImGui::SetNextWindowSize({200,80});ImGui::Begin("Other editor");ImGui::Button("Other control");ImGui::End();}
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,1100,750);glClearColor(.05f,.05f,.05f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        rowCenters.clear();coordinateCenters.clear();auto& tables=ImGui::GetCurrentContext()->Tables;
        for(int n=0;n<tables.GetMapSize();++n)if(auto* table=tables.TryGetMapData(n);table&&table->ColumnsCount==4&&table->LastFrameActive==ImGui::GetFrameCount()){
            float height=table->RowPosY2-table->RowPosY1;
            for(int row=0;row<8;++row){float y=table->RowPosY1+(row-7)*height+height*.5f;rowCenters.push_back({table->Columns[0].WorkMinX+20,y});coordinateCenters.push_back({table->Columns[1].WorkMinX+20,y});}
        }
    }
    void Click(ImVec2 point,bool shift=false){Frame(point,false,false,false,shift);Frame(point,true,false,false,shift);Frame(point,false,false,false,shift);}
    void Key(ImGuiKey key,bool shift=false){ImGui::GetIO().AddKeyEvent(key,true);Frame({-100,-100},false,false,true,shift);ImGui::GetIO().AddKeyEvent(key,false);Frame();}
    void FileButton(bool load){
        auto start=ImGui::FindWindowByName("Shape Editor")->DC.CursorStartPos;auto& style=ImGui::GetStyle();
        if(load)start.x+=ImGui::CalcTextSize("Save").x+2*style.FramePadding.x+style.ItemSpacing.x;
        Click({start.x+ImGui::CalcTextSize(load?"Load":"Save").x*.5f+style.FramePadding.x,start.y+ImGui::GetFrameHeight()*.5f});
    }
    void SaveMenu(int item){
        FileButton(false);auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;Check(!popups.empty()&&popups.back().Window,"Save opens a destination menu");
        auto start=popups.back().Window->DC.CursorStartPos;Click({start.x+40,start.y+ImGui::GetTextLineHeight()*.5f+item*ImGui::GetTextLineHeightWithSpacing()});
    }
    void EditMenu(){
        auto* window=ImGui::FindWindowByName("Shape Editor");auto rect=window->MenuBarRect();
        Click({rect.Min.x+63,(rect.Min.y+rect.Max.y)*.5f});
        auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;Check(!popups.empty()&&popups.back().Window,"Edit menu opens");
        auto* popup=popups.back().Window;
        Click({popup->DC.CursorStartPos.x+30,popup->DC.CursorStartPos.y+ImGui::GetTextLineHeight()*.5f});Frame();
        Check(panel.VertexListOpen(),"Edit > Vertex list opens the vertex UI");
    }
    void CloseVertexList(){
        ImGuiWindow* panelWindow=nullptr;for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&std::strstr(window->Name,"/Vertex panel_")){panelWindow=window;break;}
        Check(panelWindow!=nullptr,"Vertex panel window exists");
        Click({panelWindow->WorkRect.Max.x-ImGui::GetFrameHeight()*.5f,panelWindow->DC.CursorStartPos.y+ImGui::GetFrameHeight()*.5f});Frame();
        Check(!panel.VertexListOpen()&&rowCenters.empty(),"Close button hides the vertex UI");
    }
    ImGuiWindow* GeneralWindow(){
        for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&std::strstr(window->Name,"/General panel_"))return window;
        throw std::runtime_error("General panel window exists");
    }
    void GeneralButton(){
        auto start=ImGui::FindWindowByName("Shape Editor")->DC.CursorStartPos;auto& style=ImGui::GetStyle();
        start.x+=ImGui::CalcTextSize("Save").x+ImGui::CalcTextSize("Load").x+4*style.FramePadding.x+2*style.ItemSpacing.x;
        Click({start.x+ImGui::CalcTextSize("General").x*.5f+style.FramePadding.x,start.y+ImGui::GetFrameHeight()*.5f});Frame();
    }
    Vector2 Project(int row){auto camera=panel.Camera();auto index=panel.Selection().order[row];auto p=GizmoMath::Project(Mesh().custom.positions[index]-panel.LocalPivot(),panel.CanvasOrigin(),panel.CanvasSize(),camera.Get(panel.CanvasSize().x,panel.CanvasSize().y).GetVPMatrix());Check(bool(p),"Projected test vertex");return Vector2(*p);}
    ShapeEditorMath::Cube Cube(){auto camera=panel.Camera();return ShapeEditorMath::MakeCube(Mesh().custom.positions[panel.Selection().ActiveVertex()]-panel.LocalPivot(),camera.position,panel.CanvasOrigin(),panel.CanvasSize(),camera.Get(panel.CanvasSize().x,panel.CanvasSize().y).GetVPMatrix());}
    std::vector<ShapeEditorMath::TriangleMarker> Markers(){auto camera=panel.Camera();return ShapeEditorMath::TriangleMarkers(Mesh().custom,Mesh().Intersection(),camera.position,panel.CanvasOrigin(),panel.CanvasSize(),camera.Get(panel.CanvasSize().x,panel.CanvasSize().y).GetVPMatrix(),panel.LocalPivot());}
    static ImVec2 Point(Vector2 p){return {p.x,p.y};}
    void Screenshot(const fs::path& path){
        if(path.empty())return;fs::create_directories(path.parent_path());
        constexpr int width=1100,height=750;std::vector<unsigned char> pixels(width*height*4);glReadPixels(0,0,width,height,GL_BGRA,GL_UNSIGNED_BYTE,pixels.data());
        std::array<unsigned char,54> header{};header[0]='B';header[1]='M';auto put=[&](int at,uint32_t value){for(int i=0;i<4;++i)header[at+i]=static_cast<unsigned char>(value>>(8*i));};
        put(2,54+static_cast<uint32_t>(pixels.size()));put(10,54);put(14,40);put(18,width);put(22,height);header[26]=1;header[28]=32;
        std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(header.data()),header.size());out.write(reinterpret_cast<char*>(pixels.data()),pixels.size());
    }
};
static void TriangleUITests(const fs::path& directory,const fs::path& screenshot){
    auto assetRoot=directory/fs::path(u8"한글 텍스처")/NewAssetID();std::string bitmap(70,'\0');bitmap[0]='B';bitmap[1]='M';auto put=[&](int at,uint32_t value){for(int i=0;i<4;++i)bitmap[at+i]=static_cast<char>(value>>(i*8));};put(2,70);put(10,54);put(14,40);put(18,2);put(22,2);bitmap[26]=1;bitmap[28]=32;
    for(size_t i=54;i<bitmap.size();i+=4){bitmap[i]=30;bitmap[i+1]=100;bitmap[i+2]=static_cast<char>(240);bitmap[i+3]=static_cast<char>(255);}AtomicWrite(assetRoot/"face.bmp",bitmap);AssetDatabase assets(assetRoot);
    Fixture f(assetRoot);f.assets=&assets;auto markers=f.Markers();Check(!markers.empty(),"Triangle markers appear in the editor canvas");auto marker=markers.front();auto center=(marker.points[0]+marker.points[1]+marker.points[2])/3.f;
    auto countWhite=[&](){auto large=ShapeEditorMath::ScaleMarker(marker,1.3f);Vector2 lo=large.points[0],hi=lo;for(auto p:large.points){lo=glm::min(lo,p);hi=glm::max(hi,p);}int x=int(lo.x)-2,y=750-int(hi.y)-3,width=int(hi.x-lo.x)+6,height=int(hi.y-lo.y)+6;std::vector<unsigned char> pixels(width*height*4);glReadPixels(x,y,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());int count=0;for(size_t i=0;i<pixels.size();i+=4)if(pixels[i]>245&&pixels[i+1]>245&&pixels[i+2]>245)++count;return count;};
    f.Frame();int normal=countWhite();f.Frame(Fixture::Point(center));int hover=countWhite();f.Frame();Check(normal>20&&hover>normal*1.25&&hover<normal*1.65&&countWhite()==normal,"Rendered white triangles enlarge on hover and return when the pointer leaves");
    f.Click(Fixture::Point(center));f.Frame();Check(f.panel.Selection().triangle==marker.triangle&&f.panel.Selection().selected.size()==3&&f.panel.TrianglePanelOpen()&&f.panel.VertexListOpen(),"Clicking a triangle selects its vertices and opens material and vertex panels");
    for(int c=0;c<3;++c)Check(f.panel.Selection().selected.contains(f.Mesh().custom.indices[marker.triangle*3+c]),"Selection contains exactly the clicked triangle vertices");
    Check(!f.Mesh().triangleMaterials.Edited(),"Selecting a triangle alone does not create an override");
    ImGuiWindow* panel=nullptr;for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&std::strstr(window->Name,"/Triangle panel_")){panel=window;break;}Check(panel!=nullptr,"Triangle property panel exists");
    auto start=panel->DC.CursorStartPos;auto& style=ImGui::GetStyle();float row=ImGui::GetFrameHeightWithSpacing(),separator=ImGui::GetTextLineHeight()+2*style.SeparatorTextPadding.y+style.ItemSpacing.y;
    f.Click({start.x+80,start.y+row+separator+row+ImGui::GetFrameHeight()*.5f});f.Frame();
    Check(f.Mesh().triangleMaterials.faces.size()==1&&f.Mesh().triangleMaterials.faces.contains(marker.triangle),"Material sliders change only the selected triangle");
    Check(!HasTriangleMaterials(*f.editor.scene->Find(f.id)),"Triangle edits stay in the draft until saved in scene");
    auto value=f.Mesh().triangleMaterials.faces.at(marker.triangle);f.Key(ImGuiKey_Z);Check(!f.Mesh().triangleMaterials.Edited(),"Triangle material editing is undoable");f.Key(ImGuiKey_Y);Check(f.Mesh().triangleMaterials.faces.at(marker.triangle)==value,"Triangle material editing supports redo");
    f.Click({start.x+80,start.y+row+separator+row*3+ImGui::GetFrameHeight()*.5f});f.Frame();
    auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;Check(!popups.empty()&&popups.back().Window,"Triangle texture picker opens");auto option=popups.back().Window->DC.CursorStartPos;
    f.Click({option.x+35,option.y+ImGui::GetTextLineHeight()*.5f+ImGui::GetTextLineHeightWithSpacing()});f.Frame();
    Check(f.Mesh().triangleMaterials.faces.at(marker.triangle).albedoAsset==assets.Entries().front().id&&f.Mesh().triangleMaterials.faces.at(marker.triangle).albedoTexture,"Choosing a texture loads the selected asset for this triangle");
    f.Key(ImGuiKey_Z);Check(f.Mesh().triangleMaterials.faces.at(marker.triangle).albedoAsset.empty(),"Texture selection is undoable without changing the material sliders");f.Key(ImGuiKey_Y);Check(f.Mesh().triangleMaterials.faces.at(marker.triangle).albedoTexture!=nullptr,"Redo reloads the per-triangle texture");
    f.Frame();f.Screenshot(screenshot.empty()?fs::path{}:screenshot.parent_path()/"ShapeEditor-triangles.bmp");
    f.panel.SaveFile(f.editor,directory/"face-geometry.riceshape");Check(f.panel.IsDirty(f.editor)&&!HasTriangleMaterials(*f.editor.scene->Find(f.id)),"Asset-only save does not apply object-specific material edits");
    f.panel.SaveInScene(f.editor,&assets);Check(HasTriangleMaterials(*f.editor.scene->Find(f.id))&&!f.panel.IsDirty(f.editor),"In scene save applies per-triangle materials to the object");
    auto* object=f.editor.scene->Find(f.id);object->GetComponent<Material>()->enabled=true;auto unchanged=f.editor.Snapshot();
    f.materialInspector=true;f.Frame();f.Frame();auto* inspector=ImGui::FindWindowByName("Test Material Inspector");Check(inspector!=nullptr,"Material Inspector test window exists");
    auto toggle=inspector->DC.CursorStartPos;toggle.x+=8;toggle.y+=ImGui::GetFrameHeight()*.5f;f.Click(toggle);f.Frame();
    Check(f.materialPrompt.Pending(*f.editor.scene,*object)&&f.editor.Snapshot()==unchanged,"Changing an edited Material opens the warning without changing the object");
    auto warning=[&](){for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&std::strstr(window->Name,"/Overwrite warning_"))return window;throw std::runtime_error("Overwrite warning is visible");};
    auto* overlay=warning();auto y=overlay->DC.CursorPosPrevLine.y+ImGui::GetFrameHeight()*.5f;
    f.Screenshot(screenshot.empty()?fs::path{}:screenshot.parent_path()/"ShapeEditor-material-warning.bmp");
    float proceedWidth=ImGui::CalcTextSize("Proceed").x+2*style.FramePadding.x;
    f.Click({overlay->DC.CursorStartPos.x+proceedWidth+style.ItemSpacing.x+20,y});f.Frame();
    Check(!f.materialPrompt.Pending(*f.editor.scene,*object)&&f.editor.Snapshot()==unchanged,"Return dismisses the warning and retains every override");
    f.Click(toggle);f.Frame();overlay=warning();y=overlay->DC.CursorPosPrevLine.y+ImGui::GetFrameHeight()*.5f;
    f.Click({overlay->DC.CursorStartPos.x+20,y});f.Frame();
    Check(!HasTriangleMaterials(*object)&&!object->GetComponent<Material>()->enabled&&!f.Mesh().triangleMaterials.Edited(),"Proceed applies the pending value uniformly and clears the scene and open draft overrides");
    f.editor.Undo();Check(HasTriangleMaterials(*f.editor.scene->Find(f.id)),"Inspector Proceed is undoable");
}
static void InteractionTests(const fs::path& screenshot){
    Fixture f;Check(f.panel.CanvasSize().x>200&&f.panel.CanvasSize().y>200,"Shape Editor provides a usable viewport");
    Check(!f.panel.VertexListOpen()&&f.rowCenters.empty(),"Vertex UI is hidden until opened from Edit");
    auto fullWidth=f.panel.CanvasSize().x;f.EditMenu();Check(f.panel.CanvasSize().x<fullWidth-200,"Opening vertex UI makes room beside the viewport");
    auto original=f.Mesh().custom.positions;
    f.Click(Fixture::Point(f.Project(0)));Check(f.panel.Selection().active==0,"Canvas click activates a vertex cube");
    f.Click(Fixture::Point(f.Project(1)),true);Check(f.panel.Selection().selected.size()==2,"Canvas Shift click adds to selection");
    auto selected=f.panel.Selection().selected;auto cube=f.Cube();Check(!cube.faces.empty(),"Active vertex has a cube");
    auto face=cube.faces.back();Vector2 center(0);for(auto point:face.points)center+=point*.25f;
    f.Frame(Fixture::Point(center));f.Frame(Fixture::Point(center),true);f.Frame(Fixture::Point(center+Vector2(25,-15)),true);
    f.Frame(Fixture::Point(center),true);Check(f.Mesh().custom.positions==original,"Dragging back to the press position restores the original geometry");
    f.Frame(Fixture::Point(center+Vector2(25,-15)),true);f.Frame(Fixture::Point(center+Vector2(25,-15)));
    auto moved=f.Mesh().custom.positions;Vector3 translation(0);bool found=false;
    for(size_t i=0;i<original.size();++i){if(selected.contains(static_cast<uint32_t>(i))){if(!found){translation=moved[i]-original[i];found=true;}Check(Near(moved[i]-original[i],translation),"Cube drag moves selected vertices together");}else Check(Near(moved[i],original[i]),"Unselected vertices remain fixed");}
    Check(glm::length(translation)>.001f,"Cube drag changes coordinates");for(int a=0;a<3;++a)if(a!=face.axis)Check(std::abs(translation[a])<.0001f,"Cube drag follows the face normal");
    f.panel.Draft().Undo();f.Frame();Check(f.Mesh().custom.positions==original&&f.panel.IsOpen(),"Undo restores geometry while keeping the editor open");
    f.panel.Draft().Redo();f.Frame();Check(f.Mesh().custom.positions==moved,"Redo restores drag");
    cube=f.Cube();face=cube.faces.back();center=Vector2(0);for(auto point:face.points)center+=point*.25f;auto pivot=f.panel.Selection().ActiveVertex();
    f.Frame(Fixture::Point(center));f.Frame(Fixture::Point(center),false,true);f.Frame(Fixture::Point(center+Vector2(50,0)),false,true);f.Frame(Fixture::Point(center+Vector2(50,0)));
    auto rotated=f.Mesh().custom.positions;Check(Near(rotated[pivot],moved[pivot]),"Right drag leaves the clicked pivot fixed");
    bool unselectedRotated=false;for(size_t i=0;i<rotated.size();++i)if(!selected.contains(static_cast<uint32_t>(i))&&!Near(rotated[i],moved[i]))unselectedRotated=true;
    Check(unselectedRotated,"Right drag rotates the whole shape including unselected vertices");
    f.panel.Draft().Undo();f.panel.Draft().Undo();f.Frame();
    Check(f.rowCenters.size()==8,"Vertex list contains eight numbered rows");
    f.Click(f.rowCenters[0]);f.Click(f.rowCenters[3],true);Check(f.panel.Selection().selected.size()==4&&f.panel.Selection().active==3,"Vertex list supports Shift range selection");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow,true);f.Frame(f.rowCenters[3],false,false,false,true);
    Check(f.panel.Selection().cursor==4&&f.panel.Selection().active==3&&f.panel.Selection().selected.size()==5,"List Shift Down extends selection and retains the clicked rotation pivot");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow,false);f.Frame();
    f.Screenshot(screenshot.empty()?fs::path{}:screenshot.parent_path()/"ShapeEditor-cube.bmp");
    auto numericStart=f.Mesh().custom.positions;auto coord=f.coordinateCenters[0];f.Frame(coord);f.Frame(coord,true);f.Frame({coord.x+35,coord.y},true);f.Frame({coord.x+35,coord.y});
    Check(f.panel.Selection().active==0&&f.Mesh().custom.positions!=numericStart,"Editing a coordinate activates its cube and modifies geometry");
    f.panel.Draft().Undo();f.Frame();Check(f.Mesh().custom.positions==numericStart,"Coordinate drag creates one undo action");
    auto origin=f.panel.CanvasOrigin(),size=f.panel.CanvasSize();f.Frame(Fixture::Point(origin+Vector2(2)),false,false,true);f.Frame(Fixture::Point(origin+Vector2(2)),true,false,true);
    f.Frame(Fixture::Point(origin+size-Vector2(2)),true,false,true);f.Frame(Fixture::Point(origin+size-Vector2(2)),false,false,true);
    Check(!f.panel.Selection().selected.empty()&&f.panel.Selection().selected.size()<8,"Ctrl drag selects visible vertices and excludes occluded vertices");
    f.Frame();f.Screenshot(screenshot);
    auto retained=f.panel.Selection().selected;f.CloseVertexList();Check(f.panel.CanvasSize().x==fullWidth&&f.panel.Selection().selected==retained,"Closing vertex UI expands the viewport without losing selection");
    f.Screenshot(screenshot.empty()?fs::path{}:screenshot.parent_path()/"ShapeEditor-toolbar.bmp");f.EditMenu();
    // Hidden vertices can still be explicitly selected from the list.
    f.Click(f.rowCenters[6]);Check(f.panel.Selection().active==6,"List can activate any vertex, including hidden ones");
    auto before=f.Mesh().custom.positions;cube=f.Cube();face=cube.faces.back();center=Vector2(0);for(auto point:face.points)center+=point*.25f;
    f.Frame(Fixture::Point(center));f.Frame(Fixture::Point(center),true);f.Frame(Fixture::Point(center+Vector2(20,-20)),true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);f.Frame(Fixture::Point(center+Vector2(20,-20)),true);ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);f.Frame();
    Check(f.Mesh().custom.positions==before,"Escape cancels a vertex drag");
    auto other=f.editor.scene->CreateGameObject("Other selection").GetID();f.editor.Select(other);f.Frame();Check(f.panel.IsOpen(),"Changing scene selection does not retarget the open editor");
    f.editor.scene->Destroy(f.id);f.editor.scene->Flush();f.Frame();Check(!f.panel.IsOpen(),"Deleting the target closes its editor safely");
}
static void FileToolbarTests(const fs::path& directory){
    Fixture f(directory);auto path=directory/"toolbar.riceshape";
    f.nextFile=path;f.SaveMenu(2);
    Check(fs::exists(path)&&f.panel.FilePath()==fs::absolute(path)&&f.pickerModes.size()==1&&f.pickerModes.back(),"Toolbar Save button writes the chosen file");
    auto sceneBefore=f.editor.Snapshot();f.Mesh().custom.positions[0].x+=.25f;f.Mesh().custom.Touch();f.Key(ImGuiKey_S,true);
    Check(f.pickerModes.size()==1&&ShapeFile::Load(path).positions==f.Mesh().custom.positions,"Subsequent Save uses the existing filename");
    Check(f.editor.Snapshot()==sceneBefore,"Ctrl+Shift+S does not apply the draft to the scene");
    auto copy=directory/"elsewhere.riceshape";f.nextFile=copy;f.SaveMenu(2);
    Check(f.panel.FilePath()==fs::absolute(copy)&&fs::exists(copy)&&f.pickerModes.size()==2,"Elsewhere opens a picker and saves to the chosen path");
    f.externalOpen=true;f.Frame();ImGui::SetWindowFocus("Other editor");f.Frame();
    auto fileBefore=ReadFile(copy);f.Mesh().custom.positions[0].y+=.25f;f.Mesh().custom.Touch();f.Key(ImGuiKey_S,true);f.Key(ImGuiKey_O,true);
    Check(!f.panel.Focused()&&ReadFile(copy)==fileBefore&&f.editor.Snapshot()==sceneBefore&&f.panel.FilePath()==fs::absolute(copy),"Shape save shortcuts are inactive while another window is focused");
    f.externalOpen=false;ImGui::SetWindowFocus("Shape Editor");f.Frame();
    f.Key(ImGuiKey_O,true);Check(f.editor.scene->Find(f.id)->GetComponent<MeshRenderer>()->custom.positions==f.Mesh().custom.positions&&f.panel.FilePath().stem().string().find("_scene")!=std::string::npos,"Ctrl+Shift+O creates and applies a scene copy");
    auto appliedPath=f.panel.FilePath();
    auto snapshot=f.editor.Snapshot();f.Key(ImGuiKey_O);
    Check(f.editor.Snapshot()==snapshot&&f.panel.FilePath()==appliedPath&&!f.pickerModes.back(),"Cancelling Load leaves the shape and file unchanged");
    auto rect=directory/"toolbar-plane.riceshape";ShapeFile::Save(rect,ShapeMesh::FromPrimitive(BasicShape::Rectangle));f.nextFile=rect;f.FileButton(true);
    Check(f.Mesh().custom.positions.size()==4&&!f.panel.IsDirty(f.editor),"Toolbar Load button opens the chosen shape");
    f.Key(ImGuiKey_Z);Check(f.Mesh().custom.positions.size()==8&&f.panel.IsDirty(f.editor),"Toolbar Undo restores the pre-load shape");
    f.Key(ImGuiKey_Y);Check(f.Mesh().custom.positions.size()==4&&!f.panel.IsDirty(f.editor),"Toolbar Redo restores the loaded shape");
    snapshot=f.panel.Draft().Snapshot();f.nextFile=directory/"invalid.riceshape";f.Key(ImGuiKey_O);
    Check(!f.panel.FileError().empty()&&f.panel.Draft().Snapshot()==snapshot,"Invalid load shows an error without modifying the editing copy");
    f.nextFile=copy;f.Key(ImGuiKey_O);Check(f.panel.FileError().empty()&&f.Mesh().custom.positions.size()==8,"Successful load clears the error and restores editing");
}
static void GeneralTests(const fs::path& directory,const fs::path& screenshot){
    Fixture f(directory);auto& child=f.editor.scene->CreateGameObject("Attached child");child.SetParent(f.editor.scene->Find(f.id));auto childID=child.GetID();
    auto original=f.Mesh().custom.positions;auto sceneBefore=f.editor.Snapshot();auto fullWidth=f.panel.CanvasSize().x;
    f.GeneralButton();Check(f.panel.GeneralOpen()&&f.panel.CanvasSize().x<fullWidth-200,"Toolbar General opens its property panel");
    auto cameraBefore=f.panel.Camera().position;
    auto start=f.GeneralWindow()->DC.CursorStartPos;float spacing=ImGui::GetStyle().ItemSpacing.y,height=ImGui::GetFrameHeight();
    ImVec2 pivotField(start.x+25,start.y+height+spacing*2+1+height*.5f);
    f.Frame(pivotField);f.Frame(pivotField,true);f.Frame({pivotField.x+50,pivotField.y},true);f.Frame({pivotField.x+50,pivotField.y});
    Check(f.panel.DraftObject().transform.pivot.x!=.5f&&f.editor.Snapshot()==sceneBefore,"General edits pivot in the draft without changing the scene");
    f.Key(ImGuiKey_Z);Check(f.panel.DraftObject().transform.pivot==Vector3(.5f),"General pivot editing is one undo action");
    Check(Near(f.panel.Camera().position,cameraBefore),"Undoing a pivot change keeps the object in the same screen position");
    auto add=f.GeneralWindow()->DC.CursorPosPrevLine;add.y-=height+spacing;f.Click({f.GeneralWindow()->DC.CursorStartPos.x+35,add.y+height*.5f});
    Check(f.panel.DraftObject().GetComponent<Anchor>()!=nullptr&&!f.editor.scene->Find(f.id)->GetComponent<Anchor>(),"General adds an anchor only to the editing copy");
    auto anchor=[&]() -> Anchor& {return *f.panel.DraftObject().GetComponent<Anchor>();};
    auto camera=f.panel.Camera();auto cube=ShapeEditorMath::MakeCube(anchor().transform.position,camera.position,f.panel.CanvasOrigin(),f.panel.CanvasSize(),camera.Get(f.panel.CanvasSize().x,f.panel.CanvasSize().y).GetVPMatrix());
    Check(!cube.faces.empty(),"Visible anchor has a cube");auto face=cube.faces.back();Vector2 center(0);for(auto p:face.points)center+=p*.25f;
    f.Frame(Fixture::Point(center));f.Frame(Fixture::Point(center),true);f.Frame(Fixture::Point(center+Vector2(30,-20)),true);f.Frame(Fixture::Point(center+Vector2(30,-20)));
    Check(glm::length(anchor().transform.position)>.001f&&f.Mesh().custom.positions==original,"Dragging the anchor cube moves the anchor without deforming vertices");
    auto moved=anchor().transform.position;f.Key(ImGuiKey_Z);Check(Near(anchor().transform.position,Vector3(0)),"Anchor drag is undoable");f.Key(ImGuiKey_Y);Check(Near(anchor().transform.position,moved),"Anchor drag can be redone");
    camera=f.panel.Camera();cube=ShapeEditorMath::MakeCube(moved,camera.position,f.panel.CanvasOrigin(),f.panel.CanvasSize(),camera.Get(f.panel.CanvasSize().x,f.panel.CanvasSize().y).GetVPMatrix());
    face=cube.faces.back();center=Vector2(0);for(auto p:face.points)center+=p*.25f;
    f.Frame(Fixture::Point(center));f.Frame(Fixture::Point(center),false,true);f.Frame(Fixture::Point(center+Vector2(30,0)),false,true);f.Frame(Fixture::Point(center+Vector2(30,0)));
    Check(glm::length(anchor().transform.rotation)>.001f&&Near(anchor().transform.position,moved)&&f.Mesh().custom.positions==original,"Right dragging the anchor changes its orientation around its own origin");
    // Header, pivot row, description, Anchor header, Enabled, then visibility.
    start=f.GeneralWindow()->DC.CursorStartPos;
    ImVec2 visibility(start.x+height*.5f,start.y+height+spacing*2+1+(height+spacing)*3+ImGui::GetTextLineHeightWithSpacing()+height*.5f);
    f.Click(visibility);Check(!anchor().showInEditor&&anchor().enabled,"Anchor On toggle only changes editor visibility");
    f.Click(visibility);Check(anchor().showInEditor,"Anchor On toggle shows the cube again");
    f.Click({visibility.x+80,visibility.y+height+spacing});f.Frame();
    auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;Check(!popups.empty()&&popups.back().Window,"Anchor child selector opens");
    f.Screenshot(screenshot.empty()?fs::path{}:screenshot.parent_path()/"ShapeEditor-child-picker.bmp");
    auto option=popups.back().Window->DC.CursorStartPos;f.Click({option.x+40,option.y+ImGui::GetTextLineHeight()*.5f+ImGui::GetTextLineHeightWithSpacing()});
    Check(anchor().child==childID,"Anchor selector connects a direct child using the shared Inspector and General UI");
    Check(f.editor.Snapshot()==sceneBefore,"General and anchor interactions leave the scene unchanged until in scene save");
    f.Screenshot(screenshot.empty()?fs::path{}:screenshot.parent_path()/"ShapeEditor-general.bmp");
    auto* window=f.GeneralWindow();f.Click({window->WorkRect.Max.x-height*.5f,window->DC.CursorStartPos.y+height*.5f});f.Frame();
    Check(!f.panel.GeneralOpen()&&f.panel.CanvasSize().x==fullWidth,"General close button restores the viewport width");
    f.GeneralButton();Check(f.panel.GeneralOpen()&&f.panel.DraftObject().GetComponent<Anchor>(),"Reopening General preserves its properties");
}
static void FollowerUITests(const fs::path& directory,const fs::path& screenshot){
    Fixture f(directory);auto& camera=f.editor.scene->CreateGameObject("Follow camera");camera.AddComponent<CameraComponent>();auto cameraID=camera.GetID();
    f.GeneralButton();auto add=f.GeneralWindow()->DC.CursorPosPrevLine;float height=ImGui::GetFrameHeight();
    f.Click({f.GeneralWindow()->DC.CursorStartPos.x+50,add.y+height*.5f});
    Check(f.panel.DraftObject().GetComponent<CameraFollower>()&&!f.editor.scene->Find(f.id)->GetComponent<CameraFollower>(),"General adds follower only in draft");
    f.Key(ImGuiKey_Z);Check(!f.panel.DraftObject().GetComponent<CameraFollower>(),"Adding follower is undoable");
    f.Key(ImGuiKey_Y);auto* follower=f.panel.DraftObject().GetComponent<CameraFollower>();Check(follower!=nullptr,"Adding follower is redoable");
    follower->camera=cameraID;follower->transform.position={1,0,0};follower->transform.rotation={0,30,0};f.Frame();
    auto followerState=[&]()->CameraFollower&{return *f.panel.DraftObject().GetComponent<CameraFollower>();};
    auto project=[&](Vector3 position){auto view=f.panel.Camera();auto screen=GizmoMath::Project(position,f.panel.CanvasOrigin(),f.panel.CanvasSize(),view.Get(f.panel.CanvasSize().x,f.panel.CanvasSize().y).GetVPMatrix());Check(bool(screen),"Follower is visible");return Vector2(*screen);};
    auto originalMesh=f.Mesh().custom.positions;auto originalCamera=f.panel.Camera().position;auto originalRotation=followerState().transform.rotation;
    f.Click(Fixture::Point(project(followerState().transform.position)));
    Check(f.panel.FollowerSelected(),"Clicking camera icon selects follower");
    for(int index=0;index<3;++index){
        auto view=f.panel.Camera();auto axes=ShapeEditorMath::TranslationAxes(followerState().transform.position,f.panel.CanvasOrigin(),f.panel.CanvasSize(),view.Get(f.panel.CanvasSize().x,f.panel.CanvasSize().y).GetVPMatrix());
        auto found=std::find_if(axes.begin(),axes.end(),[&](const auto& a){return a.axis==index;});Check(found!=axes.end(),"Pivot axis is available");auto axis=*found;
        auto start=axis.start+(axis.end-axis.start)*.8f;auto end=start+glm::normalize(axis.end-axis.start)*24.f;auto before=followerState().transform.position;
        f.Frame(Fixture::Point(start));f.Frame(Fixture::Point(start),true);f.Frame(Fixture::Point(end),true);f.Frame(Fixture::Point(end));
        auto after=followerState().transform.position;Check(after[index]>before[index]+.001f,"Dragging follower axis moves its coordinate");
        for(int other=0;other<3;++other)if(other!=index)Check(after[other]==before[other],"Axis drag preserves other coordinates");
        Check(f.Mesh().custom.positions==originalMesh&&followerState().transform.rotation==originalRotation&&Near(f.panel.Camera().position,originalCamera),"Follower drag preserves mesh, orientation and editor camera");
        f.Key(ImGuiKey_Z);Check(Near(followerState().transform.position,before),"Follower drag is one undo action");
        f.Key(ImGuiKey_Y);Check(Near(followerState().transform.position,after),"Follower drag supports redo");f.Key(ImGuiKey_Z);
        f.Frame(Fixture::Point(start));f.Frame(Fixture::Point(start),true);f.Frame(Fixture::Point(end),true);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);f.Frame(Fixture::Point(end),true);ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);f.Frame();
        Check(Near(followerState().transform.position,before)&&!f.panel.Editing(),"Escape cancels follower drag");
    }
    followerState().enabled=false;f.Frame();Check(f.panel.FollowerSelected(),"Disabled follower remains editable");followerState().enabled=true;
    f.Screenshot(screenshot.empty()?fs::path{}:screenshot.parent_path()/"ShapeEditor-follower.bmp");
    auto destination=f.panel.SaveInScene(f.editor);auto* applied=f.editor.scene->Find(f.id)->GetComponent<CameraFollower>();
    Check(applied&&applied->camera==cameraID&&applied->transform.position==Vector3(1,0,0),"Save in scene keeps follower camera and offset");
    Check(ShapeFile::LoadDocument(destination).followerCamera==0,"Shape asset omits scene camera IDs");
}
static void RenderTests(){
    SceneRenderer renderer;ViewportTarget target;MeshRenderer mesh;mesh.SetShape(int(BasicShape::Custom));mesh.color={.8f,.6f,.4f,1};
    PerspectiveCamera camera(45,1,.1f,100);camera.SetPosition({2,2,3});camera.LookAtObj({0,0,0});
    std::vector<unsigned char> pixels(256*256*4);target.Render(256,256,[&]{renderer.RenderShape(mesh,camera);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});
    size_t count=0;for(size_t i=0;i<pixels.size();i+=4)if(pixels[i]>100){Check(std::abs(int(pixels[i])-204)<=1&&std::abs(int(pixels[i+1])-153)<=1&&std::abs(int(pixels[i+2])-102)<=1,"All unlit editor faces have constant brightness");++count;}
    Check(count>1000,"Isolated custom mesh renders");auto oldArray=mesh.customArray;
    mesh.custom.positions[0].x+=1;mesh.custom.Touch();renderer.RenderShape(mesh,camera);Check(oldArray!=mesh.customArray,"Geometry edits invalidate the GPU mesh cache");
    Scene scene;auto& object=scene.CreateGameObject("Scene custom");auto& sceneMesh=object.AddComponent<MeshRenderer>();sceneMesh.SetShape(int(BasicShape::Custom));sceneMesh.color={.8f,.6f,.4f,1};
    // RenderShape must not leave unlit uniforms enabled for the regular scene.
    target.Render(256,256,[&]{renderer.Render(scene,camera);glReadPixels(128,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});
    Check(std::abs(int(pixels[0])-31)<=1,"Scene rendering restores its normal lighting after the isolated editor");
    for(auto& p:sceneMesh.custom.positions)p.x+=2;sceneMesh.custom.Touch();camera.SetPosition({2,0,4});camera.LookAtObj({2,0,0});
    target.Render(256,256,[&]{renderer.Render(scene,camera);glReadPixels(128,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});
    Check(std::abs(int(pixels[0])-31)<=1,"Scene renders edited custom vertices at their changed positions");
}
class SolidTexture final:public Texture2D {
    GLuint texture=0;
public:
    explicit SolidTexture(std::array<unsigned char,4> color){glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,color.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);}
    ~SolidTexture(){glDeleteTextures(1,&texture);}
    uint32_t GetWidth()const override{return 1;}uint32_t GetHeight()const override{return 1;}
    void Bind(uint32_t slot)const override{glActiveTexture(GL_TEXTURE0+slot);glBindTexture(GL_TEXTURE_2D,texture);}
};
static void TriangleRenderTests(){
    Scene scene;auto& object=scene.CreateGameObject("Two face colors");auto& mesh=object.AddComponent<MeshRenderer>();mesh.SetShape(int(BasicShape::Custom));mesh.custom=ShapeMesh::FromPrimitive(BasicShape::Rectangle);mesh.color=Color(1);
    SurfaceMaterial red,blue;red.albedoAsset="red";blue.albedoAsset="blue";red.albedoTexture=CreateRef<SolidTexture>(std::array<unsigned char,4>{255,0,0,255});blue.albedoTexture=CreateRef<SolidTexture>(std::array<unsigned char,4>{0,0,255,255});mesh.triangleMaterials.faces[0]=red;mesh.triangleMaterials.faces[1]=blue;
    PerspectiveCamera camera(45,1,.1f,100);camera.SetPosition({0,0,2});camera.LookAtObj({0,0,0});SceneRenderer renderer;ViewportTarget target;
    auto sample=[&](bool editor,int triangle){std::array<unsigned char,4> result{};Vector3 center(0);for(int c=0;c<3;++c)center+=mesh.custom.positions[mesh.custom.indices[triangle*3+c]]/3.f;auto projected=camera.GetVPMatrix()*Vector4(center,1);int x=int((projected.x/projected.w+1)*128),y=int((projected.y/projected.w+1)*128);
        target.Render(256,256,[&]{if(editor)renderer.RenderShape(mesh,camera);else renderer.Render(scene,camera);glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,result.data());});return result;};
    auto a=sample(true,0),b=sample(true,1);Check(a[0]>250&&a[2]<2&&b[0]<2&&b[2]>250,"Each triangle uses its own texture in the unlit shape editor");
    a=sample(false,0);b=sample(false,1);Check(a[0]>35&&a[2]<2&&b[0]<2&&b[2]>35,"Scene and game rendering retain per-triangle textures");
    auto& light=scene.CreateGameObject("Triangle light").AddComponent<Light>();light.type=1;light.direction={0,0,-1};light.intensity=2;
    a=sample(false,0);mesh.triangleMaterials.faces[0].metallicity=1;mesh.triangleMaterials.faces[0].roughness=.1f;b=sample(false,0);
    Check(std::abs(int(a[0])-int(b[0]))>3,"Per-triangle material properties affect game lighting");
    auto& other=scene.CreateGameObject("Unedited object");auto& otherMesh=other.AddComponent<MeshRenderer>();otherMesh.SetShape(int(BasicShape::Custom));otherMesh.custom=mesh.custom;otherMesh.color=Color(1);object.activeSelf=false;light.enabled=false;
    a=sample(false,0);Check(std::abs(int(a[0])-38)<=1&&a[0]==a[1]&&a[1]==a[2],"Drawing an edited object does not leak textures or material flags into another object");
}
int main(int argc,char** argv){GLFWwindow* window=nullptr;try{
    fs::path screenshot=argc>1?fs::path(argv[1]):fs::path{};auto files=(screenshot.empty()?fs::path("TestData"):screenshot.parent_path())/"ShapeFiles";
    DataTests();GeometryTests();TriangleDataTests();FileTests(files);Check(glfwInit()!=0,"GLFW init");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(1100,750,"Shape Editor tests",nullptr,nullptr);Check(window!=nullptr,"Hidden OpenGL context");glfwMakeContextCurrent(window);Check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))!=0,"Load OpenGL");glEnable(GL_DEPTH_TEST);
    RenderTests();TriangleRenderTests();InteractionTests(screenshot);TriangleUITests(files,screenshot);FileToolbarTests(files);GeneralTests(files,screenshot);FollowerUITests(files,screenshot);Check(glGetError()==GL_NO_ERROR,"Shape editor leaves no GL errors");
    glfwDestroyWindow(window);glfwTerminate();std::cout<<"Custom mesh, serialization, selection, gizmo, rendering and UI interaction tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}}
