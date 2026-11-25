#include "Editor/SceneInspector.h"
#include <iostream>
using namespace Rice;
namespace Rice {
struct SceneInspectorTestAccess {
    static void CloseProject(SceneInspector& inspector){inspector.QueueProjectAction(SceneInspector::ProjectAction::Close);}
    static void OpenProject(SceneInspector& inspector,const fs::path& path){inspector.QueueProjectAction(SceneInspector::ProjectAction::Open,path);}
    static SceneInspector::CloseResult CloseDialog(SceneInspector& inspector,Workspace& workspace){return inspector.ProjectCloseDialog(workspace);}
    static ShapeEditor& Shape(SceneInspector& inspector){return inspector.shapeEditor;}
    static bool DragOrbit(SceneInspector& inspector,const Scene& scene,EntityID target,Vector3 pivot,CameraWheelGesture::Result wheel){
        inspector.orbitTarget=target;
        inspector.orbitPivot=pivot;
        return inspector.DragSceneCameraOrbit(scene,wheel);
    }
    static fs::path AvailableBuildFile(const fs::path& file){return SceneInspector::AvailableBuildFile(file);}
    static void Playback(SceneInspector& inspector,Workspace& workspace){inspector.DrawPlaybackControls(workspace);}
    static void Footer(SceneInspector& inspector,Workspace& workspace,GameObject& object,const std::string& name){
        inspector.ComponentFooter(workspace,object,name);
    }
    static void Property(SceneInspector& inspector,Workspace& workspace,GameObject& object,const char* label,Rice::Property property){
        inspector.DrawObjectProperty(workspace,object,label,property);
    }
    static void EndFrame(SceneInspector& inspector,Workspace& workspace){inspector.EndFrameEdits(workspace);}
    static void Settings(SceneInspector& inspector,Workspace& workspace){inspector.SceneControls(workspace);}
    static void QueueMaterial(SceneInspector& inspector,Workspace& workspace,GameObject& object){
        auto value=MaterialSettings(object.GetComponent<Material>());value.roughness=.9f;
        inspector.materialPrompt.Queue(*workspace.editor.scene,object,value);
    }
    static bool MaterialPending(SceneInspector& inspector,Workspace& workspace,GameObject& object){return inspector.materialPrompt.Pending(*workspace.editor.scene,object);}
    static void ResetInteraction(SceneInspector& inspector,Workspace& workspace){inspector.ResetSceneInteraction(workspace);}
    static void ConfigureHeadless(SceneInspector& inspector){
        inspector.layoutInitialized=true;inspector.resetLayout=false;
        inspector.sceneOpen=inspector.gameOpen=inspector.projectOpen=inspector.consoleOpen=inspector.inputOpen=false;
        inspector.hierarchyOpen=inspector.inspectorOpen=true;
    }
};
}
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void TestSceneCameraOrbit(){
    Scene scene;
    SceneInspector inspector;
    CameraWheelGesture gesture;
    auto press=gesture.Update({0,0},true,true,true,false);
    auto drag=gesture.Update({40,-15},false,true,true,false);
    auto& target=scene.CreateGameObject("Orbit target");
    target.transform.position={5,2,-3};
    const auto id=target.GetID();
    const Vector3 pickedPivot=target.transform.position;
    Check(!SceneInspectorTestAccess::DragOrbit(inspector,scene,0,pickedPivot,press),"A middle press alone leaves the camera still");
    auto verify=[&](EntityID hit,Vector3 pivot){
        auto& camera=inspector.SceneCamera(800,600);
        const Vector3 position=camera.GetPosition();
        const Vector4 anchor=camera.GetViewMatrix()*Vector4(pivot,1);
        Check(SceneInspectorTestAccess::DragOrbit(inspector,scene,hit,pickedPivot,drag),"Middle drag must orbit even when the crosshair has no target");
        auto& after=inspector.SceneCamera(800,600);
        Check(glm::length(after.GetPosition()-position)>.01f,"Orbit changes the camera position");
        Check(glm::length(after.GetViewMatrix()*Vector4(pivot,1)-anchor)<.001f,"Orbit preserves the anchor location relative to the camera");
    };
    verify(id,pickedPivot);
    verify(0,Vector3(0));
    scene.Destroy(id);
    scene.Flush();
    verify(id,Vector3(0));
}
struct SnapshotProbe : Component {int value=0;};
struct Fixture {
    Workspace workspace;
    SceneInspector inspector;
    EntityID objectID;
    ImVec2 origin{};
    bool popup=false,playback=false,properties=false,settings=false,fullEditor=false;
    ImVec2 numberOrigin{},nameOrigin{};
    std::string component="Light";
    Fixture(const fs::path& root):workspace(root,true){
        auto& object=workspace.editor.scene->CreateGameObject("Footer test");objectID=object.GetID();object.AddComponent<Light>();
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;
        io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    }
    ~Fixture(){ImGui::DestroyContext();}
    GameObject& Object(){return *workspace.editor.scene->Find(objectID);}
    void Draw(ImVec2 mouse={-1,-1},bool down=false){
        auto& io=ImGui::GetIO();io.AddMousePosEvent(mouse.x,mouse.y);io.AddMouseButtonEvent(0,down);
        ImGui::NewFrame();
        if(fullEditor){
            inspector.Draw(workspace,{},[](int,int,bool)->Matrix4{throw std::runtime_error("Headless editor must not render a viewport");});
            ImGui::Render();return;
        }
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({400,400});
        ImGui::Begin("Inspector test",nullptr,ImGuiWindowFlags_NoSavedSettings);
        if(playback)SceneInspectorTestAccess::Playback(inspector,workspace);
        else if(settings)SceneInspectorTestAccess::Settings(inspector,workspace);
        else if(properties){
            numberOrigin=ImGui::GetCursorScreenPos();
            SceneInspectorTestAccess::Property(inspector,workspace,Object(),"Intensity",&Object().GetComponent<Light>()->intensity);
            nameOrigin=ImGui::GetCursorScreenPos();
            SceneInspectorTestAccess::Property(inspector,workspace,Object(),"Name",&Object().name);
        }
        else {
        ImGui::BeginChild("Component box",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY);
        ImGui::TextUnformatted("Component");origin=ImGui::GetCursorScreenPos();
        SceneInspectorTestAccess::Footer(inspector,workspace,Object(),component);
        popup=ImGui::IsPopupOpen("Add component");
        ImGui::EndChild();}
        ImGui::End();SceneInspectorTestAccess::EndFrame(inspector,workspace);ImGui::Render();
    }
    void Click(ImVec2 point){Draw(point);Draw(point,true);Draw(point,true);Draw(point);}
    ImVec2 FindControl(const char* label){
        auto* window=ImGui::FindWindowByName("Inspector test");auto id=window->GetID(label);
        for(float y=window->Pos.y+20;y<window->Pos.y+70;y+=3)for(float x=window->Pos.x+5;x<window->Pos.x+200;x+=3){
            ImVec2 point{x,y};Draw(point);if(ImGui::GetCurrentContext()->HoveredId==id)return point;
        }
        throw std::runtime_error(std::string("Control missing: ")+label);
    }
    ImVec2 FindCheckbox(const char* label){
        auto* window=ImGui::FindWindowByName("Inspector test");auto id=window->GetID(label);
        for(float y=window->Pos.y+20;y<window->Pos.y+window->Size.y-10;y+=2){
            ImVec2 point{window->Pos.x+14,y};Draw(point);if(ImGui::GetCurrentContext()->HoveredId==id)return point;
        }
        throw std::runtime_error(std::string("Checkbox missing: ")+label);
    }
    ImVec2 FindToolbar(const char* label){
        auto* window=ImGui::FindWindowByName("##MainMenuBar");Check(window,"Toolbar missing");
        // BeginMenuBar adds this ID scope, which has been popped after Draw.
        auto id=ImHashStr(label,0,ImHashStr("##MenuBar",0,window->ID));
        const auto position=window->Pos;const auto size=window->Size;
        for(float x=position.x+2;x<position.x+size.x-2;x+=3){
            ImVec2 point{x,position.y+size.y*.5f};Draw(point);if(ImGui::GetCurrentContext()->HoveredId==id)return point;
        }
        throw std::runtime_error(std::string("Toolbar control missing: ")+label);
    }
    ImVec2 FindMenu(const char* label,int depth){
        auto& stack=ImGui::GetCurrentContext()->OpenPopupStack;
        Check(stack.Size>depth&&stack[depth].Window,"Menu window missing");
        auto* window=stack[depth].Window;auto id=window->GetID(label);
        auto position=window->Pos;auto size=window->Size;
        for(float y=position.y+4;y<position.y+size.y-2;y+=3){
            ImVec2 point{position.x+40,y};Draw(point);
            if(ImGui::GetCurrentContext()->HoveredId==id)return point;
        }
        throw std::runtime_error(std::string("Menu item missing: ")+label);
    }
};

void TestEditEvents(const fs::path& root){
    Fixture test(root/NewAssetID());test.properties=true;test.Draw();test.Draw();
    auto& editor=test.workspace.editor;
    auto captures=editor.SnapshotCaptureCount();
    for(int frame=0;frame<30;++frame)test.Draw();
    Check(editor.SnapshotCaptureCount()==captures&&editor.UndoCount()==0,"Idle property drawing does not capture snapshots or create undo records");
    auto number=ImVec2(test.numberOrigin.x+40,test.numberOrigin.y+7);
    test.Click(number);
    Check(editor.SnapshotCaptureCount()==captures&&editor.UndoCount()==0,"Focusing a field without changing it does not capture a snapshot");
    // A second immediate click is ImGui's numeric text-entry gesture, not a drag.
    for(int frame=0;frame<30;++frame)test.Draw();
    auto original=test.Object().GetComponent<Light>()->intensity;
    test.Draw(number);test.Draw(number,true);test.Draw(number,true);
    for(int step=1;step<=8;++step)test.Draw({number.x+float(step*8),number.y},true);
    auto changed=test.Object().GetComponent<Light>()->intensity;
    Check(changed!=original&&editor.UndoCount()==0,"An active drag updates the field without committing intermediate undo records");
    Check(editor.SnapshotCaptureCount()==captures+1,"A drag captures its starting value only once");
    test.Draw({number.x+64,number.y});
    Check(editor.UndoCount()==1&&editor.SnapshotCaptureCount()==captures+2,"Mouse release captures the final drag value and commits one undo action");
    editor.Undo();Check(test.Object().GetComponent<Light>()->intensity==original,"Undo restores the value before the actual drag");
    editor.Redo();Check(test.Object().GetComponent<Light>()->intensity==changed,"Redo restores the value after the drag");

    auto oldName=test.Object().name;
    auto name=ImVec2(test.nameOrigin.x+40,test.nameOrigin.y+7);
    test.Click(name);
    auto& io=ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);test.Draw(name);
    io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);test.Draw(name);
    io.AddInputCharactersUTF8("Renamed by input");test.Draw(name);
    Check(test.Object().name=="Renamed by input"&&editor.UndoCount()==1,"Active text editing does not commit on each character");
    test.Click({350,350});
    Check(editor.UndoCount()==2,"Leaving the text field commits one undo action");
    editor.Undo();Check(test.Object().name==oldName,"Text undo uses the value before the first character change");
    editor.Redo();Check(test.Object().name=="Renamed by input","Text redo restores the committed name");
}

void TestIdleEditor(const fs::path& root){
    Fixture test(root/NewAssetID());
    SceneInspectorTestAccess::ConfigureHeadless(test.inspector);test.fullEditor=true;
    test.workspace.editor.Select(test.objectID);
    for(int i=0;i<1000;++i)test.workspace.editor.scene->CreateGameObject("Grass").AddComponent<MeshRenderer>();
    int unselectedFieldVisits=0;
    test.workspace.registry.Register<SnapshotProbe>("SnapshotProbe",[&](auto& probe,auto& visit){++unselectedFieldVisits;visit("Value",&probe.value);});
    test.workspace.editor.scene->CreateGameObject("Unselected snapshot probe").AddComponent<SnapshotProbe>();
    auto captures=test.workspace.editor.SnapshotCaptureCount();
    auto objects=test.workspace.editor.CapturedObjectCount();
    for(int frame=0;frame<5;++frame)test.Draw();
    test.FindToolbar("Import...");
    Check(test.workspace.editor.SnapshotCaptureCount()==captures&&test.workspace.editor.CapturedObjectCount()==objects,
          "Idle full editor frames with 1000 cubes capture no object or scene snapshots");
    Check(test.workspace.editor.UndoCount()==0,"Idle full editor frames create no undo history");
    Check(unselectedFieldVisits==0,"Idle full editor drawing never serializes or captures unselected component fields");
}

void TestSceneSettings(const fs::path& root){
    Fixture test(root/NewAssetID());test.settings=true;test.Draw();test.Draw();
    auto& editor=test.workspace.editor;auto old=editor.scene->settings.ground;
    auto capturedObjects=editor.CapturedObjectCount();
    auto checkbox=test.FindCheckbox("Ground (Y = 0)");test.Click(checkbox);
    Check(editor.scene->settings.ground!=old&&editor.UndoCount()==1,"Clicking a scene setting creates one undo action");
    Check(editor.CapturedObjectCount()==capturedObjects,"A scene settings widget captures no object data");
    editor.Undo();Check(editor.scene->settings.ground==old,"Undo restores the scene setting before the checkbox click");
    editor.Redo();Check(editor.scene->settings.ground!=old,"Redo reapplies the scene setting after the checkbox click");
}

void TestComponentControls(const fs::path& root){
    Fixture test(root/NewAssetID());test.Draw();test.Draw();
    auto minus=ImVec2(test.origin.x+26+ImGui::GetStyle().ItemSpacing.x+13,test.origin.y+6);
    test.Click(minus);Check(!test.Object().GetComponent<Light>(),"Detach must complete on mouse release after holding button");
    Check(test.workspace.editor.UndoCount()==1,"Component detach button records one edit");
    test.workspace.editor.Undo();Check(test.Object().GetComponent<Light>()!=nullptr,"Component detach undo reattaches the original component");
    test.workspace.editor.Redo();Check(!test.Object().GetComponent<Light>(),"Component detach redo removes it again");
    test.component="Transform";test.Draw();test.Draw();
    auto plus=ImVec2(test.origin.x+13,test.origin.y+6);test.Click(plus);
    Check(test.popup,"Attach menu must open on mouse release after holding button");
    test.Draw(plus);auto category=test.FindMenu("Audio",0);test.Click(category);
    for(int i=0;i<20;++i)test.Draw(category);
    auto item=test.FindMenu("AudioSource",1);test.Click(item);
    Check(test.Object().GetComponent<AudioSource>()!=nullptr,"Selecting category component must attach it");
    Check(!test.popup,"Attach menu closes after selection");
    test.workspace.editor.Undo();Check(!test.Object().GetComponent<AudioSource>(),"Component attach menu records an undoable addition");
    test.workspace.editor.Redo();Check(test.Object().GetComponent<AudioSource>()!=nullptr,"Component attach redo restores the addition");
    test.Draw();test.Draw();minus={test.origin.x+26+ImGui::GetStyle().ItemSpacing.x+13,test.origin.y+6};test.Click(minus);
    Check(test.Object().GetComponent<AudioSource>()!=nullptr,"Transform detach must stay disabled");
    test.component="AudioSource";test.Draw();test.Draw();test.workspace.editor.playing=true;test.Click(minus);
    Check(test.Object().GetComponent<AudioSource>()!=nullptr,"Play mode detach stays disabled");
    test.workspace.editor.playing=false;test.Click(minus);Check(!test.Object().GetComponent<AudioSource>(),"Attached component can be detached");
    test.playback=true;test.Draw();test.Draw();
    test.Click(test.FindControl("Play"));Check(test.workspace.editor.playing&&!test.workspace.editor.paused,"Play button starts simulation");
    test.Click(test.FindControl("Pause"));Check(test.workspace.editor.paused,"Pause button pauses simulation");
    test.Click(test.FindControl("Resume"));Check(test.workspace.editor.playing&&!test.workspace.editor.paused,"Resume button continues simulation");
    test.Click(test.FindControl("Pause"));test.Click(test.FindControl("Stop"));
    Check(!test.workspace.editor.playing&&!test.workspace.editor.paused,"Stop button exits paused simulation");
}

void TestMaterialProposalHistory(const fs::path& root){
    Fixture test(root/NewAssetID());auto& editor=test.workspace.editor;
    auto& mesh=test.Object().AddComponent<MeshRenderer>();mesh.SetShape(int(BasicShape::Custom));mesh.triangleMaterials.faces[0]=SurfaceMaterial{};
    test.Object().AddComponent<Material>();editor.Select(test.objectID);
    editor.Edit([&](Scene& scene){scene.Find(test.objectID)->name="Edited name";});
    SceneInspectorTestAccess::ConfigureHeadless(test.inspector);test.fullEditor=true;test.Draw();test.Draw();
    auto queue=[&]{SceneInspectorTestAccess::QueueMaterial(test.inspector,test.workspace,test.Object());};
    auto pending=[&]{return SceneInspectorTestAccess::MaterialPending(test.inspector,test.workspace,test.Object());};
    auto scene=editor.scene->GetInstanceID();

    queue();test.Draw();Check(pending(),"An idle frame preserves the pending material proposal");
    test.Click(test.FindToolbar("Undo"));
    Check(editor.scene->GetInstanceID()==scene&&test.Object().name=="Footer test"&&!pending(),"Toolbar Undo invalidates a material proposal even though the scene instance is retained");
    queue();test.Click(test.FindToolbar("Redo"));
    Check(test.Object().name=="Edited name"&&!pending(),"Toolbar Redo invalidates the previous material proposal");

    queue();auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_Z,true);test.Draw();
    Check(editor.RedoCount()==1&&!pending(),"Undo shortcut clears a stale material proposal before Inspector drawing");
    io.AddKeyEvent(ImGuiKey_Z,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);test.Draw();
    queue();editor.Undo();test.Draw();Check(pending(),"An empty Undo does not discard a valid pending proposal");
    editor.Redo();test.Draw();Check(!pending(),"An external Redo is observed and cancels the proposal on the next editor draw");
    queue();SceneInspectorTestAccess::ResetInteraction(test.inspector,test.workspace);
    Check(!pending(),"Resetting scene interactions cancels pending material proposals");
}
void TestProjectClose(const fs::path& output){
    using Result=SceneInspector::CloseResult;
    Fixture test(output/(NewAssetID()+"-closing"));auto& w=test.workspace;auto& ui=test.inspector;
    auto frame=[&]{
        ImGui::NewFrame();auto result=SceneInspectorTestAccess::CloseDialog(ui,w);ImGui::Render();return result;
    };
    auto choose=[&](const char* label){
        frame();
        auto* modal=ImGui::FindWindowByName("Unsaved project changes");
        Check(modal&&modal->Active,"Unsaved project must display the three-way choice");
        auto id=modal->GetID(label);
        auto& io=ImGui::GetIO();
        for(float y=modal->Pos.y+5;y<modal->Pos.y+modal->Size.y;y+=5)
            for(float x=modal->Pos.x+5;x<modal->Pos.x+modal->Size.x;x+=5){
                io.AddMousePosEvent(x,y);frame();
                if(ImGui::GetCurrentContext()->HoveredId!=id)continue;
                io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);return frame();
            }
        throw std::runtime_error(std::string("Close choice inaccessible: ")+label);
    };
    w.Save();auto first=ReadFile(w.project->FilePath());
    ui.RequestExit();Check(frame()==Result::Exit,"Clean project exits immediately");
    test.Object().name="Unsaved object";
    SceneInspectorTestAccess::CloseProject(ui);Check(frame()==Result::None,"Dirty Close Project waits for a choice");
    ui.RequestExit();Check(choose("Go Back")==Result::None&&w.HasUnsavedChanges(),"Going back retains the edited scene");
    Check(ReadFile(w.project->FilePath())==first,"Go Back leaves the project file untouched");
    SceneInspectorTestAccess::CloseProject(ui);frame();ui.RequestExit();
    Check(choose("Don't Save")==Result::CloseProject,"Repeated native close does not change the pending action");
    Check(ReadFile(w.project->FilePath())==first,"Don't Save leaves the project file untouched");
    ui.RequestExit();frame();Check(choose("Save")==Result::Exit,"Save completes the requested exit");
    Check(!w.HasUnsavedChanges()&&w.project->LoadScene(w.registry)->Find(test.objectID)->name=="Unsaved object","Saved edits survive reopening");
    auto& mesh=test.Object().AddComponent<MeshRenderer>();mesh.SetShape(int(BasicShape::Custom));w.Save();
    auto& shape=SceneInspectorTestAccess::Shape(ui);shape.Open(w.editor,test.objectID,w.project->assets.Root());
    Check(!shape.HasUnsavedChanges(),"Viewing a custom mesh does not dirty the project");
    auto& draft=shape.DraftObject().GetComponent<MeshRenderer>()->custom;draft.positions[0].x+=1;draft.Touch();
    SceneInspectorTestAccess::OpenProject(ui,w.project->root/"missing.rice");frame();
    Check(choose("Don't Save")==Result::None&&shape.HasUnsavedChanges(),"Failed Open preserves the shape draft");
    Check(choose("Go Back")==Result::None,"Failed Open can be cancelled");
    ui.RequestExit();frame();Check(choose("Save")==Result::Exit,"A shape-only edit prompts and saves");
    Check(!w.project->LoadScene(w.registry)->Find(test.objectID)->GetComponent<MeshRenderer>()->shapeAsset.empty(),"Closing with Save applies the custom shape to the saved scene");
    test.Object().AddComponent<SnapshotProbe>();ui.RequestExit();frame();
    Check(choose("Save")==Result::None,"A serialization failure leaves the project open");
    Check(choose("Don't Save")==Result::Exit,"The user can still discard after a failed save");
}
void TestEmptyEditor(const fs::path& output){
    auto root=output/(NewAssetID()+"-empty");fs::create_directories(root);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,720};io.DeltaTime=1.f/60;
    io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    {
        SceneInspector inspector;
        auto frame=[&]{
            ImGui::NewFrame();auto result=inspector.DrawEmpty(root/"Engine"/"Runtime");
            ImGui::Render();return result;
        };
        for(int i=0;i<120;++i)Check(!frame(),"Idle empty editor does not request a project");
        for(const char* window:{"Hierarchy","Inspector###Inspector","Scene","Game","Project","Console","Input"})
            Check(ImGui::FindWindowByName(window)!=nullptr,"Empty editor creates all dock panels");
        inspector.ReportOpenError("Invalid project");Check(!frame(),"Failed open retains empty editor");
        Check(!fs::exists(root/"Project")&&!fs::exists(root/"Assets")&&!fs::exists(root/"Logs"),"Empty editor does not create project or diagnostic files");
        auto click=[&](ImGuiWindow* window,ImGuiID button){
            for(float y=window->Pos.y+5;y<window->Pos.y+window->Size.y;y+=5)
                for(float x=window->Pos.x+5;x<window->Pos.x+window->Size.x;x+=5){
                    io.AddMousePosEvent(x,y);frame();
                    if(ImGui::GetCurrentContext()->HoveredId!=button)continue;
                    io.AddMouseButtonEvent(0,true);frame();
                    io.AddMouseButtonEvent(0,false);return frame();
                }
            throw std::runtime_error("Project action button is inaccessible");
        };
        auto toolbar=ImGui::FindWindowByName("##MainMenuBar");
        auto fileMenu=ImHashStr("File",0,ImHashStr("##MenuBar",0,toolbar->ID));
        click(toolbar,fileMenu);frame();
        auto& menus=ImGui::GetCurrentContext()->OpenPopupStack;
        Check(!menus.empty()&&menus.back().Window,"File menu opens from the empty editor");
        auto* menu=menus.back().Window;
        Check(!click(menu,menu->GetID("New Project...")),"File > New Project waits for a name and location");
        auto dialog=ImGui::FindWindowByName("New Project");Check(dialog&&dialog->Active,"New Project opens the creation form");
        auto created=click(dialog,dialog->GetID("Create"));
        Check(created&&created->create&&created->path==fs::absolute(root)/"Projects"/"New Project","New Project returns the selected folder");
        auto build=root/"Builds"/"New Project.exe";
        Check(SceneInspectorTestAccess::AvailableBuildFile(build)==build,"A first build uses the project name");
        AtomicWrite(build,"existing build");AtomicWrite(root/"Builds"/"New Project (2).exe","existing build");
        Check(SceneInspectorTestAccess::AvailableBuildFile(build)==root/"Builds"/"New Project (3).exe","Numbered build suggestion preserves existing games");
    }
    ImGui::DestroyContext();
}

int main(int argc,char** argv){try{
    Check(argc==2,"Test directory required");
    TestSceneCameraOrbit();
    TestEmptyEditor(fs::path(argv[1]));TestProjectClose(fs::path(argv[1]));
    TestComponentControls(fs::path(argv[1]));TestEditEvents(fs::path(argv[1]));TestIdleEditor(fs::path(argv[1]));TestSceneSettings(fs::path(argv[1]));TestMaterialProposalHistory(fs::path(argv[1]));
    std::cout<<"Inspector component actions passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
