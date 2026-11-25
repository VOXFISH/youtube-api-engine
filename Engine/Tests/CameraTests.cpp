#include "Editor/EditorClipboard.h"
#include "Editor/EditorCamera.h"
#include "Project/ShapeFile.h"
#include <iostream>
using namespace Rice;
static void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static bool Near(Vector3 a,Vector3 b){return glm::length(a-b)<.001f;}
static bool Near(Matrix4 a,Matrix4 b){for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>.001f)return false;return true;}
static void CheckFrameImportedHierarchy(){
    Scene scene;
    auto& root=scene.CreateGameObject("Imported");root.transform.position={80,12,-30};
    auto& piece=scene.CreateGameObject("Piece");piece.SetParent(&root);
    piece.transform.position={.02f,0,0};piece.transform.localScale=Vector3(.008f);
    piece.AddComponent<MeshRenderer>();
    auto& unrelated=scene.CreateGameObject("Other");unrelated.AddComponent<MeshRenderer>();
    unrelated.transform.localScale=Vector3(100);
    EditorCamera camera;
    camera.Get(400,800);
    camera.FrameHierarchy(scene,root.GetID());
    auto vp=camera.Get(400,800).GetVPMatrix();
    float minX=1,maxX=-1;
    for(int corner=0;corner<8;++corner){
        Vector4 local(corner&1?.5f:-.5f,corner&2?.5f:-.5f,corner&4?.5f:-.5f,1);
        auto clip=vp*piece.GetWorldMatrix()*local;
        Check(clip.w>0,"Framed geometry lies in front of the editor camera");
        Vector3 ndc=Vector3(clip)/clip.w;
        Check(glm::all(glm::lessThanEqual(glm::abs(ndc),Vector3(1))),"Framing keeps tiny imported geometry inside the viewport and clip planes");
        minX=std::min(minX,ndc.x);maxX=std::max(maxX,ndc.x);
    }
    Check(maxX-minX>.4f,"Framing makes a distant import visible without including unrelated scene geometry");
}
int main(){try{
    CheckFrameImportedHierarchy();
    ComponentRegistry registry;EditorSession editor(registry);auto& scene=*editor.scene;
    auto& cameraObject=scene.CreateGameObject("Camera");auto& camera=cameraObject.AddComponent<CameraComponent>();
    cameraObject.transform.position={3,4,8};cameraObject.transform.pivot={1,0,2};
    Check(camera.LookAt({0,0,0}),"LookAt accepts target");
    Check(Near(cameraObject.GetWorldRotation()*Vector3(0,0,-1),glm::normalize(-cameraObject.GetWorldPivot())),"LookAt rotates object forward");
    auto before=cameraObject.transform.rotation;Check(!camera.LookAt(cameraObject.GetWorldPivot())&&before==cameraObject.transform.rotation,"Coincident LookAt is safe");
    Check(camera.LookAt(cameraObject.GetWorldPivot()+Vector3(0,1,0)),"Vertical LookAt is safe");
    Check(Near(cameraObject.GetWorldRotation()*Vector3(0,0,-1),Vector3(0,1,0)),"Vertical forward");
    auto& parent=scene.CreateGameObject("Parent");parent.transform.rotation={20,50,-15};parent.transform.localScale={2,.8f,3};
    cameraObject.SetParent(&parent);Check(camera.LookAt({1,2,3}),"Parented LookAt");
    Check(Near(cameraObject.GetWorldRotation()*Vector3(0,0,-1),glm::normalize(Vector3(1,2,3)-cameraObject.GetWorldPivot())),"Parented LookAt direction");
    auto& object=scene.CreateGameObject("Follower");object.AddComponent<MeshRenderer>().SetShape(int(BasicShape::Custom));object.SetParent(&parent);object.transform.pivot={0,1,-1};
    auto& follower=object.AddComponent<CameraFollower>();follower.camera=cameraObject.GetID();follower.transform.position={1,2,-3};follower.transform.rotation={10,25,35};follower.transform.localScale={1.2f,.8f,2};
    scene.Update(.01f);scene.LateUpdate(.01f);
    auto frame=glm::translate(Matrix4(1),cameraObject.GetWorldPivot())*glm::mat4_cast(cameraObject.GetWorldRotation());
    Check(Near(object.GetPivotMatrix()*follower.transform.GetMatrix(),frame),"Pivot-relative follower aligns under scaled parent");
    auto world=object.GetWorldMatrix();follower.enabled=false;cameraObject.transform.position.x+=3;scene.LateUpdate(.01f);Check(Near(world,object.GetWorldMatrix()),"Disabled follower does not move");
    follower.enabled=true;Check(follower.Follow(),"Enabled follower resumes");
    follower.transform.localScale.x=0;world=object.GetWorldMatrix();Check(!follower.Follow()&&Near(world,object.GetWorldMatrix()),"Singular offset is safe");follower.transform.localScale.x=1;
    auto loaded=SceneCodec(registry).Decode(editor.Snapshot());Check(loaded->Find(object.GetID())->GetComponent<CameraFollower>()->camera==cameraObject.GetID(),"Scene retains camera reference");
    auto doc=ShapeDocument::Capture(object);auto restored=ShapeFile::DecodeDocument(ShapeFile::Encode(doc));Check(restored.hasFollower&&restored.followerCamera==0&&restored.follower.position==follower.transform.position,"Shape stores local frame without scene reference");
    auto parentID=parent.GetID();auto snapshot=editor.Snapshot();follower.transform.position.x+=2;editor.Record(snapshot);auto id=object.GetID();editor.Undo();Check(editor.scene->Find(id)->GetComponent<CameraFollower>()->transform.position==doc.follower.position,"Undo follower edit");editor.Redo();Check(editor.scene->Find(id)->GetComponent<CameraFollower>()->transform.position.x==doc.follower.position.x+2,"Redo follower edit");
    EditorClipboard clipboard;clipboard.CopyObject(*editor.scene,parentID,registry);auto copied=clipboard.PasteObject(editor,registry);EntityID copiedCamera=0;CameraFollower* copiedFollower=nullptr;
    for(auto& o:editor.scene->GetObjects())if(o->GetParent()&&o->GetParent()->GetID()==copied){if(o->GetComponent<CameraComponent>())copiedCamera=o->GetID();if(auto* f=o->GetComponent<CameraFollower>())copiedFollower=f;}
    Check(copiedCamera&&copiedFollower&&copiedFollower->camera==copiedCamera,"Clipboard remaps copied camera reference");
    editor.scene->Destroy(copiedCamera);editor.scene->Flush();Check(!copiedFollower->Follow(),"Deleted camera is safe");
    std::cout<<"Camera and follower checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
