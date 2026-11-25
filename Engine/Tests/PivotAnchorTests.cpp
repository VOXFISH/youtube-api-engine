#include "Editor/EditorClipboard.h"
#include "Project/Prefab.h"
#include "Project/ShapeFile.h"
#include <iostream>
using namespace Rice;
static void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static bool Near(Vector3 a,Vector3 b){return glm::length(a-b)<.0002f;}
static bool Near(Matrix4 a,Matrix4 b){for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>.0003f)return false;return true;}
int main(){try{
    ComponentRegistry registry;EditorSession editor(registry);auto& parent=editor.scene->CreateGameObject("Parent");auto pid=parent.GetID();
    parent.AddComponent<MeshRenderer>().SetShape(int(BasicShape::Custom));
    parent.transform.pivot={0,0,0};Check(Near(parent.GetLocalPivot(),Vector3(-.5f)),"Zero pivot is the low corner");
    parent.transform.pivot={1,1,1};Check(Near(parent.GetLocalPivot(),Vector3(.5f)),"One pivot is the high corner");
    parent.transform.pivot={-1,2,.25f};Check(Near(parent.GetLocalPivot(),{-1.5f,1.5f,-.25f}),"Pivot fractions are not clamped");
    parent.transform.position={2,3,4};parent.transform.rotation={10,25,40};parent.transform.localScale={2,.5f,3};
    auto p=parent.GetLocalPivot();auto expected=glm::translate(Matrix4(1),p)*parent.transform.GetMatrix()*glm::translate(Matrix4(1),-p);
    Check(Near(parent.GetWorldMatrix(),expected),"Rotation and scale use the normalized pivot");
    Check(Near(parent.GetWorldPivot(),parent.transform.position+p),"Rotating and scaling keep the pivot fixed");
    auto before=parent.GetWorldMatrix();parent.SetWorldMatrix(before);Check(Near(parent.GetWorldMatrix(),before)&&parent.transform.pivot==Vector3(-1,2,.25f),"World transform conversion preserves pivot");
    auto& child=editor.scene->CreateGameObject("Child");auto cid=child.GetID();child.AddComponent<MeshRenderer>();child.SetParent(&parent);
    child.transform.position={8,-4,2};child.transform.rotation={-15,33,7};child.transform.localScale={.8f,1.2f,.7f};child.transform.pivot={1,.2f,-.3f};
    auto& anchor=parent.AddComponent<Anchor>();anchor.child=cid;anchor.transform.position={.7f,.8f,-.1f};anchor.transform.rotation={15,60,25};anchor.transform.localScale={.5f,2,1.2f};
    auto attached=parent.GetPivotMatrix()*anchor.transform.GetMatrix();
    Check(Near(child.GetWorldPivot(),Vector3(attached[3])),"Child pivot snaps to parent anchor despite an existing translation");
    auto pose=child.transform;pose.position=Vector3(0);expected=attached*pose.GetMatrix()*glm::translate(Matrix4(1),-child.GetLocalPivot());
    Check(Near(child.GetWorldMatrix(),expected),"Child inherits anchor rotation and scale");
    anchor.showInEditor=false;Check(Near(child.GetWorldPivot(),Vector3(attached[3])),"Hiding the anchor gizmo does not detach the child");
    child.transform.position={-100,30,20};Check(Near(child.GetWorldPivot(),Vector3(attached[3])),"Attached child translation cannot break the pivot constraint");
    parent.transform.rotation={43,-12,51};anchor.transform.position.x+=2;attached=parent.GetPivotMatrix()*anchor.transform.GetMatrix();
    Check(Near(child.GetWorldPivot(),Vector3(attached[3])),"Moving and rotating parent or anchor updates attachment immediately");
    auto saved=SceneCodec(registry).Decode(editor.Snapshot());auto* savedParent=saved->Find(pid);auto* savedChild=saved->Find(cid);
    Check(savedParent->transform.pivot==parent.transform.pivot&&savedParent->GetComponent<Anchor>()->child==cid,"Scene persistence retains pivot and anchor binding");
    Check(Near(savedChild->GetWorldPivot(),Vector3(attached[3])),"Restored scene maintains the attachment");
    auto doc=ShapeDocument::Capture(parent);auto restored=ShapeFile::DecodeDocument(ShapeFile::Encode(doc));
    Check(restored.hasAnchor&&restored.pivot==doc.pivot&&restored.anchor.position==doc.anchor.position&&restored.anchor.rotation==doc.anchor.rotation&&restored.anchor.localScale==doc.anchor.localScale&&!restored.anchorShown,"Shape files retain General properties");
    std::ostringstream legacy;legacy<<std::setprecision(9)<<"RICE_SHAPE 1\n";doc.mesh.Write(legacy);
    auto legacyDoc=ShapeFile::DecodeDocument(legacy.str());Check(legacyDoc.pivot==Vector3(.5f)&&!legacyDoc.hasAnchor,"Old shape files receive a centered pivot");
    Scene oldScene;auto& oldObject=oldScene.CreateGameObject("Old custom");auto& oldMesh=oldObject.AddComponent<MeshRenderer>();oldMesh.SetShape(int(BasicShape::Custom));
    for(auto& vertex:oldMesh.custom.positions)vertex+=Vector3(2,-1,3);oldMesh.custom.Touch();oldObject.transform.rotation={20,30,40};oldObject.transform.localScale={2,.7f,3};
    auto oldMatrix=oldObject.transform.GetMatrix();auto currentText=SceneCodec(registry).Encode(oldScene);
    auto firstObject=currentText.find("Object ");Check(firstObject!=std::string::npos,"Legacy scene fixture contains an object");
    // v3 has neither the later Settings/EditorGrid records nor per-object pivot values.
    auto oldText=std::string("RICE_SCENE 3\n")+currentText.substr(firstObject);auto objectEnd=oldText.find('\n',oldText.find("Object "));auto pivotStart=oldText.rfind("0.5 0.5 0.5 ",objectEnd);
    Check(pivotStart!=std::string::npos,"Legacy scene fixture contains the pivot suffix");oldText.erase(pivotStart,objectEnd-pivotStart);
    auto migrated=SceneCodec(registry).Decode(oldText);Check(Near(migrated->Find(oldObject.GetID())->GetWorldMatrix(),oldMatrix),"Pre-pivot scenes retain the pose of offset custom geometry");
    auto& duplicate=SceneCodec(registry).Duplicate(*editor.scene,pid);Check(duplicate.GetComponent<Anchor>()->child==0,"A single-object duplicate does not retain another parent's child binding");
    EditorClipboard clipboard;clipboard.CopyObject(*editor.scene,pid,registry);auto copied=clipboard.PasteObject(editor,registry);
    auto* copiedParent=editor.scene->Find(copied);auto copiedChild=copiedParent->GetComponent<Anchor>()->child;
    Check(copiedChild!=cid&&editor.scene->Find(copiedChild)->GetParent()==copiedParent,"Clipboard remaps anchor child to the copied hierarchy");
    Check(Near(editor.scene->Find(copiedChild)->GetWorldPivot(),Vector3((copiedParent->GetPivotMatrix()*copiedParent->GetComponent<Anchor>()->transform.GetMatrix())[3])),"Copied attachment remains aligned");
    auto prefab=PrefabAsset::Decode(PrefabAsset::Capture(*editor.scene,pid,registry)->Encode(),registry);
    auto instance=prefab->Create(*editor.scene,registry,{7,8,9},false);auto* instanceParent=editor.scene->Find(instance.front());auto* instanceAnchor=instanceParent->GetComponent<Anchor>();
    Check(instanceAnchor->child!=cid&&editor.scene->Find(instanceAnchor->child)->GetParent()==instanceParent,"Prefab remaps anchor child references");
    Check(Near(editor.scene->Find(instanceAnchor->child)->GetWorldPivot(),Vector3((instanceParent->GetPivotMatrix()*instanceAnchor->transform.GetMatrix())[3])),"Prefab attachment follows its placed parent");
    child.SetParent(nullptr);Check(anchor.child==0&&!child.ParentAnchor(),"Reparenting clears the old anchor binding");child.SetParent(&parent);anchor.child=cid;
    editor.scene->Destroy(cid);editor.scene->Flush();Check(anchor.child==0,"Deleting the linked child restores none");
    std::cout<<"Pivot, anchor transforms, persistence and copied bindings passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
