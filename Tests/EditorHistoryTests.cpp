#include "Project/SceneCodec.h"
#include <iostream>

using namespace Rice;

void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}

void ScopedObjects(ComponentRegistry& registry){
    EditorSession editor(registry);
    auto id=editor.scene->CreateGameObject("Original").GetID();
    auto otherID=editor.scene->CreateGameObject("Unrelated").GetID();
    editor.scene->Find(id)->AddComponent<Light>().intensity=2.f;

    auto before=editor.CaptureObject(id);
    // A continuous interaction may update a value over many frames, with only
    // its two endpoints entering the undo history.
    for(int frame=0;frame<30;++frame)editor.scene->Find(id)->transform.position.x=float(frame+1);
    editor.scene->Find(id)->name="Dragged";
    editor.scene->Find(id)->GetComponent<Light>()->intensity=4.f;
    editor.scene->Find(otherID)->name="Unrelated external change";
    editor.Record(before);
    editor.Undo();
    Check(editor.scene->Find(id)->name=="Original"&&editor.scene->Find(id)->transform.position.x==0.f&&editor.scene->Find(id)->GetComponent<Light>()->intensity==2.f,
          "One scoped undo restores every field at interaction start");
    Check(editor.scene->Find(otherID)->name=="Unrelated external change","Scoped undo leaves unrelated object data untouched");
    auto noop=editor.CaptureObject(id);editor.Record(noop);
    editor.Redo();
    Check(editor.scene->Find(id)->name=="Dragged"&&editor.scene->Find(id)->transform.position.x==30.f&&editor.scene->Find(id)->GetComponent<Light>()->intensity==4.f,
          "No-op interaction preserves redo and the final drag value");
    Check(editor.scene->Find(otherID)->name=="Unrelated external change","Scoped redo leaves unrelated data untouched");
    editor.Undo();editor.Undo();
    Check(editor.scene->Find(id)->name=="Original"&&editor.scene->Find(id)->transform.position.x==0.f,"Continuous interaction creates exactly one undo action");
    editor.Redo();

    before=editor.CaptureObject(id);
    editor.scene->Find(id)->RemoveComponent<Light>();
    editor.scene->Find(id)->AddComponent<BoxCollider>().size={2,3,4};
    editor.scene->Flush();editor.Record(before);
    editor.Undo();
    Check(editor.scene->Find(id)->GetComponent<Light>()&&editor.scene->Find(id)->GetComponent<Light>()->intensity==4.f&&!editor.scene->Find(id)->GetComponent<BoxCollider>(),
          "Scoped undo restores detached components and removes newly attached components");
    editor.Redo();
    Check(!editor.scene->Find(id)->GetComponent<Light>()&&editor.scene->Find(id)->GetComponent<BoxCollider>()->size==Vector3(2,3,4),
          "Scoped redo restores component attachment and serialized properties");
}

void MultipleObjectsAndSettings(ComponentRegistry& registry){
    EditorSession editor(registry);
    auto parent=editor.scene->CreateGameObject("Parent").GetID();
    auto child=editor.scene->CreateGameObject("Child").GetID();
    auto other=editor.scene->CreateGameObject("Other").GetID();
    editor.scene->Find(child)->SetParent(editor.scene->Find(parent));
    editor.scene->Find(parent)->AddComponent<Anchor>().child=child;
    auto before=editor.CaptureObjects({parent,child});
    editor.scene->Find(child)->SetParent(editor.scene->Find(other));
    editor.scene->Find(child)->transform.position={2,4,6};
    editor.Record(before);
    editor.Undo();
    Check(editor.scene->Find(child)->GetParent()==editor.scene->Find(parent)&&editor.scene->Find(parent)->GetComponent<Anchor>()->child==child,
          "Multi-object undo restores parent links and the parent anchor reference");
    editor.Redo();
    Check(editor.scene->Find(child)->GetParent()==editor.scene->Find(other)&&editor.scene->Find(parent)->GetComponent<Anchor>()->child==0&&editor.scene->Find(child)->transform.position==Vector3(2,4,6),
          "Multi-object redo restores the complete reparent operation");

    auto settings=editor.CaptureSettings();
    auto old=editor.scene->settings;
    editor.scene->settings.ground=!old.ground;
    editor.scene->settings.lightIntensity=7.f;
    editor.scene->settings.skyboxAsset="skybox-test";
    editor.scene->settings.axes={0,1,0};
    editor.scene->Find(other)->name="Changed outside settings";
    editor.Record(settings);
    editor.Undo();
    Check(editor.scene->settings.ground==old.ground&&editor.scene->settings.lightIntensity==old.lightIntensity&&editor.scene->settings.skyboxAsset==old.skyboxAsset&&editor.scene->settings.axes==old.axes,
          "Settings undo restores scene settings and editor guides");
    Check(editor.scene->Find(other)->name=="Changed outside settings","Settings undo does not restore objects");
    editor.Redo();
    Check(editor.scene->settings.ground!=old.ground&&editor.scene->settings.lightIntensity==7.f&&editor.scene->settings.skyboxAsset=="skybox-test"&&editor.scene->settings.axes==Vector3(0,1,0),
          "Settings redo reapplies every changed setting");
}

void StructuralEditsAndPlay(ComponentRegistry& registry){
    EditorSession editor(registry);
    auto persistent=editor.scene->CreateGameObject("Persistent").GetID();
    EntityID parent=0,child=0;
    auto original=editor.Snapshot();
    editor.Edit([&](Scene& scene){
        auto& root=scene.CreateGameObject("Created parent");parent=root.GetID();
        auto& leaf=scene.CreateGameObject("Created child");child=leaf.GetID();leaf.SetParent(&root);
        leaf.AddComponent<Light>().intensity=3.f;
    });
    auto created=editor.Snapshot();
    editor.Undo();Check(editor.Snapshot()==original,"Structural undo removes the created hierarchy");
    editor.Redo();Check(editor.Snapshot()==created&&editor.scene->Find(child)->GetParent()->GetID()==parent,"Structural redo preserves IDs, hierarchy, order and components");
    editor.Edit([&](Scene& scene){scene.Destroy(parent);});
    Check(!editor.scene->Find(parent)&&!editor.scene->Find(child),"Deleting a parent removes its hierarchy");
    editor.Undo();Check(editor.Snapshot()==created,"Undo restores a deleted hierarchy");
    editor.Redo();Check(editor.Snapshot()==original,"Redo deletes the restored hierarchy");
    editor.Undo();

    auto scoped=editor.CaptureObject(persistent);editor.scene->Find(persistent)->name="Before Play";editor.Record(scoped);
    auto editScene=editor.Snapshot();
    editor.Play();
    Check(editor.playing,"Play starts simulation");
    editor.scene->Find(persistent)->name="Runtime mutation";
    editor.scene->CreateGameObject("Runtime object");
    editor.Stop();
    Check(!editor.playing&&!editor.paused&&editor.Snapshot()==editScene,"Stop restores the exact pre-Play scene and removes runtime objects");
    editor.Undo();Check(editor.scene->Find(persistent)->name=="Persistent","Undo history remains valid across Play and Stop");
    editor.Redo();Check(editor.scene->Find(persistent)->name=="Before Play","Redo history remains valid across Play and Stop");
}

void CaptureBoundaries(ComponentRegistry& registry){
    EditorSession editor(registry);
    EntityID edited=0;
    for(int i=0;i<1000;++i){auto& object=editor.scene->CreateGameObject("Grass");object.AddComponent<MeshRenderer>();edited=object.GetID();}
    auto count=editor.CapturedObjectCount(),captures=editor.SnapshotCaptureCount();
    auto before=editor.CaptureObject(edited);
    Check(editor.CapturedObjectCount()==count+1&&editor.SnapshotCaptureCount()==captures+1,
          "Beginning a scoped edit captures only the selected object in a 1000-object scene");
    for(int i=0;i<60;++i)editor.scene->Find(edited)->transform.position.x=float(i);
    Check(editor.CapturedObjectCount()==count+1&&editor.SnapshotCaptureCount()==captures+1,
          "Intermediate edit frames do not capture snapshots");
    editor.Record(before);
    Check(editor.CapturedObjectCount()==count+2&&editor.SnapshotCaptureCount()==captures+2&&editor.UndoCount()==1,
          "Ending a scoped edit captures only the changed object and adds one history entry");
    Check(editor.HistoryObjectCount()==1,"Scoped history retains only the changed object");
    auto settings=editor.CaptureSettings();editor.scene->settings.ground=!editor.scene->settings.ground;editor.Record(settings);
    Check(editor.CapturedObjectCount()==count+2,"Settings transactions never capture scene objects");
    Check(editor.HistoryObjectCount()==1,"Settings history does not retain any object snapshots");
    editor.Edit([&](Scene& scene){scene.Find(edited)->name="Changed structurally";});
    Check(editor.HistoryObjectCount()==2,"A generic edit retains only changed objects from its boundary captures");

    before=editor.CaptureObject(edited);
    auto replacement=CreateScope<Scene>();replacement->CreateGameObject("Replacement",edited);
    editor.Replace(std::move(replacement));editor.Record(before);
    Check(editor.UndoCount()==0&&editor.scene->Find(edited)->name=="Replacement","A snapshot from a replaced document cannot create undo history in the new scene");
    before=editor.CaptureObject(edited);editor.scene->Find(edited)->name="Fresh edit";editor.Record(before);
    auto stale=editor.CaptureObject(edited);editor.Undo();editor.Record(stale);
    Check(editor.UndoCount()==0&&editor.RedoCount()==1&&editor.scene->Find(edited)->name=="Replacement",
          "Undo invalidates an in-flight snapshot without destroying redo history");
    stale=editor.CaptureObject(edited);editor.Redo();editor.Record(stale);
    Check(editor.UndoCount()==1&&editor.RedoCount()==0&&editor.scene->Find(edited)->name=="Fresh edit",
          "Redo invalidates an in-flight snapshot without adding a stale history record");
}

void MeshStateAndRegistration(ComponentRegistry& registry){
    EditorSession editor(registry);auto id=editor.scene->CreateGameObject("Custom shape").GetID();
    auto& mesh=editor.scene->Find(id)->AddComponent<MeshRenderer>();mesh.SetShape(int(BasicShape::Custom));
    auto initial=mesh.custom.positions;
    auto before=editor.CaptureObject(id);mesh.custom.Touch();editor.Record(before);
    Check(editor.UndoCount()==0,"A mesh cache revision change alone does not create scene history");
    before=editor.CaptureObject(id);mesh.custom.positions[0].x+=2.f;mesh.custom.Touch();editor.Record(before);
    auto changed=mesh.custom.positions;auto changedRevision=mesh.custom.revision;
    editor.Undo();auto& restored=editor.scene->Find(id)->GetComponent<MeshRenderer>()->custom;
    Check(restored.positions==initial&&restored.revision>changedRevision,"Undo restores mesh geometry and invalidates its GPU cache monotonically");
    auto undoRevision=restored.revision;editor.Redo();auto& redone=editor.scene->Find(id)->GetComponent<MeshRenderer>()->custom;
    Check(redone.positions==changed&&redone.revision>undoRevision,"Redo restores independent mesh snapshot data and invalidates its GPU cache");

    struct Unregistered : Component {};
    EditorSession unregistered(registry);auto object=unregistered.scene->CreateGameObject("Not registered").GetID();
    unregistered.scene->Find(object)->AddComponent<Unregistered>();
    bool rejected=false,mutated=false;
    try{unregistered.Edit([&](Scene&){mutated=true;});}catch(const std::exception&){rejected=true;}
    Check(rejected&&!mutated&&unregistered.UndoCount()==0,"Editing unregistered component state fails before invoking the mutation");
}

void GroupAnchorDeletion(ComponentRegistry& registry){
    EditorSession editor(registry);
    auto parent=editor.scene->CreateGameObject("Anchor parent").GetID();
    auto group=editor.scene->CreateGameObject("Group").GetID();editor.scene->Find(group)->isGroup=true;
    auto child=editor.scene->CreateGameObject("Anchored child").GetID();
    editor.scene->Find(child)->SetParent(editor.scene->Find(parent));
    auto& anchor=editor.scene->Find(parent)->AddComponent<Anchor>();anchor.child=child;anchor.transform.position={1,2,3};
    editor.scene->Find(child)->SetGroup(editor.scene->Find(group));
    auto original=editor.Snapshot();
    editor.Edit([&](Scene& scene){scene.Destroy(group);});
    Check(!editor.scene->Find(group)&&!editor.scene->Find(child)&&editor.scene->Find(parent)->GetComponent<Anchor>()->child==0,
          "Deleting a group removes members and clears their external anchor reference");
    editor.Undo();
    Check(editor.Snapshot()==original&&editor.scene->Find(child)->GetGroup()==editor.scene->Find(group)&&editor.scene->Find(child)->GetParent()==editor.scene->Find(parent),
          "Undo recreates group members and restores external anchor references and bind matrices");
    editor.Redo();
    Check(!editor.scene->Find(group)&&!editor.scene->Find(child)&&editor.scene->Find(parent)->GetComponent<Anchor>()->child==0,
          "Redo deletes the recreated group and clears its restored anchor reference");
}

void RegistrationChangeFailure(){
    struct Custom : Component {float value=2.f;};
    ComponentRegistry registry;bool changed=false;
    registry.Register<Custom>("Custom",[&](auto& component,auto& field){field(changed?"Changed schema":"Value",&component.value);});
    EditorSession editor(registry);auto id=editor.scene->CreateGameObject("Schema test").GetID();editor.scene->Find(id)->AddComponent<Custom>();
    auto before=editor.CaptureObject(id);editor.scene->Find(id)->RemoveComponent<Custom>();editor.scene->Flush();editor.Record(before);
    auto current=editor.Snapshot();changed=true;bool rejected=false;
    try{editor.Undo();}catch(const std::exception&){rejected=true;}
    Check(rejected&&editor.Snapshot()==current&&editor.UndoCount()==1&&editor.RedoCount()==0,
          "Undo rejects a changed component schema before mutating live data or history stacks");
    changed=false;editor.Undo();
    Check(editor.scene->Find(id)->GetComponent<Custom>()&&editor.scene->Find(id)->GetComponent<Custom>()->value==2.f&&editor.UndoCount()==0&&editor.RedoCount()==1,
          "A failed undo can be retried after restoring the component registration");
}

struct LegacyLifecycleProbe : Component {
    inline static int awakenings=0,destructions=0;
    void Awake()override{++awakenings;}
    void OnDestroy()override{++destructions;}
};
void LegacySnapshotCompatibility(){
    ComponentRegistry registry;registry.Register<LegacyLifecycleProbe>("Legacy lifecycle probe",[](auto&,auto&){});
    EditorSession editor(registry);auto id=editor.scene->CreateGameObject("Before").GetID();
    editor.scene->Find(id)->AddComponent<LegacyLifecycleProbe>();
    LegacyLifecycleProbe::awakenings=LegacyLifecycleProbe::destructions=0;
    auto before=editor.Snapshot();editor.scene->Find(id)->name="After";editor.Record(before);
    Check(LegacyLifecycleProbe::awakenings==0&&LegacyLifecycleProbe::destructions==0&&editor.UndoCount()==1,
          "Recording a legacy text snapshot never constructs or destroys user components");
    editor.Undo();
    Check(editor.scene->Find(id)->name=="Before"&&LegacyLifecycleProbe::awakenings==1&&LegacyLifecycleProbe::destructions==1,
          "Legacy text undo reconstructs the selected endpoint only when requested");
    editor.Record(editor.Snapshot());
    Check(LegacyLifecycleProbe::awakenings==1&&LegacyLifecycleProbe::destructions==1&&editor.RedoCount()==1,
          "A no-op legacy record preserves redo without component lifecycle callbacks");
    editor.Redo();
    Check(editor.scene->Find(id)->name=="After"&&LegacyLifecycleProbe::awakenings==2&&LegacyLifecycleProbe::destructions==2,
          "Legacy text redo restores its final endpoint and component lifecycle");
}

int main(){try{
    ComponentRegistry registry;
    ScopedObjects(registry);MultipleObjectsAndSettings(registry);StructuralEditsAndPlay(registry);CaptureBoundaries(registry);
    MeshStateAndRegistration(registry);GroupAnchorDeletion(registry);RegistrationChangeFailure();LegacySnapshotCompatibility();
    std::cout<<"Scoped editor history, no-op interactions, structural edits and Play/Stop passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
