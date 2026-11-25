#include "Editor/GizmoMath.h"
#include "Editor/TransformGizmo.h"
#include "Editor/EditorCamera.h"
#include "Project/SceneCodec.h"
#include <iostream>
using namespace Rice;
void Check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);}
bool Near(float a,float b){return std::abs(a-b)<.002f;}
struct PreviewProbe:Component {inline static int updates=0,starts=0;void Start()override{++starts;}void Update(float)override{++updates;}};
void TestEditorInput(){
    ComponentRegistry registry;registry.Register<PreviewProbe>("PreviewProbe",[](auto&,auto&){});
    EditorSession editor(registry);auto& object=editor.scene->CreateGameObject();auto id=object.GetID();object.AddComponent<MeshRenderer>();object.AddComponent<PreviewProbe>();object.AddComponent<Rigidbody>().velocity={2,3,4};editor.Select(id);
    TransformGizmo gizmo;EditorCamera navigationCamera;CameraPointerGesture pointer;bool cameraEnabled=false,objectEnabled=true;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    auto vp=glm::ortho(-2.f,2.f,-1.5f,1.5f,.1f,100.f)*glm::lookAt(Vector3(0,0,5),Vector3(0),Vector3(0,1,0));
    bool canvasHovered=false,legacyHovered=false;
    auto frame=[&](float x,float y,bool left,bool right){
        io.AddMousePosEvent(x,y);io.AddMouseButtonEvent(0,left);io.AddMouseButtonEvent(1,right);
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({800,600});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
        ImGui::Begin("Scene test",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        auto origin=ImGui::GetCursorScreenPos();auto extent=ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("Canvas",extent,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
        canvasHovered=ImGui::IsItemHovered();legacyHovered=ImGui::IsWindowHovered();
        bool cameraWasActive=pointer.Active();
        gizmo.BeginFrame();if(objectEnabled)gizmo.Draw(editor,origin,extent,vp,id,canvasHovered&&!cameraWasActive);else gizmo.Cancel(editor);
        int pressed=ImGui::IsMouseClicked(1)?1:ImGui::IsMouseClicked(0)?0:-1;
        auto navigation=pointer.Update({x,y},pressed,left,right,canvasHovered,!cameraEnabled||(!cameraWasActive&&objectEnabled&&(gizmo.Active()||gizmo.consumed||gizmo.handleHovered)));
        if(glm::length(navigation.delta)>0){if(navigation.button==1)navigationCamera.Look(navigation.delta);else navigationCamera.Pan(navigation.delta,extent.y);}
        if(objectEnabled&&canvasHovered&&!gizmo.Active()&&!gizmo.consumed&&(cameraEnabled?navigation.click==1:ImGui::IsMouseClicked(1)))ImGui::OpenPopup("Object context");
        Check(!ImGui::IsPopupOpen("Object context"),"Handle gesture never opens object popup");
        ImGui::End();ImGui::PopStyleVar();ImGui::Render();
    };
    frame(407,300,false,false);frame(407,300,false,false);frame(407,300,true,false);
    Check(canvasHovered&&gizmo.Active(),"Canvas active item starts drag within original hit radius");
    Check(!legacyHovered,"Regression reproduced: active canvas blocks window hover");
    frame(447,330,true,false);auto position=editor.scene->Find(id)->transform.position;
    Check(position.x>.1f&&position.y<-.1f&&Near(position.z,0),"Edit-mode drag moves only on face plane");
    Check(editor.previewRevision>0&&PreviewProbe::updates==0&&PreviewProbe::starts==0,"Preview does not execute gameplay lifecycle");
    Check(editor.scene->Find(id)->GetComponent<Rigidbody>()->velocity==Vector3(2,3,4),"Preview leaves simulation velocity untouched");
    frame(447,330,false,false);Check(!gizmo.Active(),"Release ends gesture");editor.Undo();Check(glm::length(editor.scene->Find(id)->transform.position)<.001f,"Drag undo restores initial transform");editor.Redo();Check(glm::length(editor.scene->Find(id)->transform.position-position)<.001f,"Drag redo");editor.Undo();
    auto& occluder=editor.scene->CreateGameObject("foreground");occluder.transform.position={0,0,2};occluder.AddComponent<MeshRenderer>();
    Check(GizmoMath::Pick(*editor.scene,{{0,0,5},{0,0,-1}})==occluder.GetID(),"Foreground object occupies handle pixel");
    frame(400,300,false,false);frame(400,300,false,true);Check(gizmo.Active()&&gizmo.consumed,"Right face handle owns context input");
    frame(450,300,false,true);frame(400,250,false,true);
    Check(Near(editor.scene->Find(id)->transform.rotation.z,90),"Orbiting front circle rotates around opposite-circle axis");
    Check(glm::length(editor.scene->Find(id)->transform.position)<.001f,"Orbit keeps object center fixed");
    frame(850,700,false,true);Check(gizmo.Active()&&gizmo.consumed,"Gesture remains captured outside canvas");
    frame(850,700,false,false);Check(!gizmo.Active()&&gizmo.consumed,"Release frame remains consumed");editor.Undo();
    frame(400,300,false,false);frame(400,300,true,false);frame(430,300,true,false);gizmo.Cancel(editor);Check(glm::length(editor.scene->Find(id)->transform.position)<.001f,"Cancel restores preview");frame(430,300,false,false);
    auto& contactTarget=editor.scene->CreateGameObject("contact target");contactTarget.transform.position={1.5f,0,0};contactTarget.AddComponent<MeshRenderer>();
    frame(400,300,false,false);frame(400,300,true,false);frame(504,300,true,false);
    Check(Near(editor.scene->Find(id)->transform.position.x,.5f),"Mouse drag applies contact snap through preview path");
    frame(510,300,true,false);Check(Near(editor.scene->Find(id)->transform.position.x,.5f),"Mouse snap holds at 10 pixels");
    frame(511,300,true,false);Check(editor.scene->Find(id)->transform.position.x>.55f,"Mouse snap releases past 10 pixels");
    frame(511,300,false,false);editor.Undo();Check(Near(editor.scene->Find(id)->transform.position.x,0),"Snapped drag is one undo action");
    auto fixedCorner=Vector3(editor.scene->Find(id)->GetWorldMatrix()*Vector4(-.5f,-.5f,-.5f,1));
    frame(500,200,false,false);frame(500,200,true,false);frame(520,180,true,false);
    Check(editor.scene->Find(id)->transform.localScale.x>1&&editor.scene->Find(id)->transform.position.x>0,"Square drag moves grabbed side and object center");
    Check(glm::length(Vector3(editor.scene->Find(id)->GetWorldMatrix()*Vector4(-.5f,-.5f,-.5f,1))-fixedCorner)<.001f,"Mouse resize keeps opposite corner fixed");
    frame(520,180,false,false);editor.Undo();Check(Near(editor.scene->Find(id)->transform.localScale.x,1)&&Near(editor.scene->Find(id)->transform.position.x,0),"Anchored resize undoes position and scale together");
    cameraEnabled=true;auto cameraBefore=navigationCamera.position;float yawBefore=navigationCamera.yaw;
    frame(407,300,false,false);frame(407,300,true,false);frame(447,330,true,false);
    Check(gizmo.Active()&&!pointer.Active()&&navigationCamera.position==cameraBefore,"Both enabled: left handle drag cannot also pan camera");
    frame(447,330,false,false);editor.Undo();
    frame(400,300,false,false);frame(400,300,false,true);frame(450,300,false,true);frame(400,250,false,true);
    Check(gizmo.Active()&&!pointer.Active()&&navigationCamera.yaw==yawBefore,"Both enabled: right handle drag cannot also rotate camera");
    frame(400,250,false,false);editor.Undo();
    auto unchanged=editor.Snapshot();frame(750,500,false,false);frame(750,500,true,false);frame(720,480,true,false);
    Check(pointer.Active()&&!gizmo.Active()&&navigationCamera.position!=cameraBefore&&editor.Snapshot()==unchanged,"Background left drag only pans camera");frame(720,480,false,false);
    frame(750,500,false,false);frame(750,500,false,true);frame(720,480,false,true);
    Check(pointer.Active()&&!gizmo.Active()&&navigationCamera.yaw!=yawBefore&&editor.Snapshot()==unchanged,"Background right drag only looks, never opens object context");frame(720,480,false,false);
    objectEnabled=false;cameraBefore=navigationCamera.position;frame(400,300,false,false);frame(400,300,true,false);frame(440,320,true,false);
    Check(!gizmo.Active()&&navigationCamera.position!=cameraBefore&&editor.Snapshot()==unchanged,"Object disabled: handles cannot consume camera drag or change scene");frame(440,320,false,false);
    cameraEnabled=false;cameraBefore=navigationCamera.position;frame(400,300,true,false);frame(450,350,true,false);frame(450,350,false,false);
    Check(!gizmo.Active()&&!pointer.Active()&&navigationCamera.position==cameraBefore&&editor.Snapshot()==unchanged,"Both disabled: left/right editing and navigation remain inactive");
    editor.Play();auto revision=editor.previewRevision;Check(!editor.PreviewTransform(id,Transform{})&&editor.previewRevision==revision,"Preview cannot modify runtime scene");editor.Stop();
    ImGui::DestroyContext();
}
void TestSurfaceSnap(){
    Scene scene;auto& moving=scene.CreateGameObject("moving");moving.AddComponent<MeshRenderer>();auto& target=scene.CreateGameObject("target");target.AddComponent<MeshRenderer>();target.transform.position={3,0,0};
    GizmoMath::SurfaceSnap snap;auto apply=[&](Vector3 value){return snap.Apply(value,[](Vector3 translation)->std::optional<Vector2>{return Vector2(translation);});};snap.Reset(scene,moving,{0,0,1});
    Check(Near(apply({1.9f,0,0}).x,1.9f),"Surface does not snap before contact");
    Check(Near(apply({2,0,0}).x,2)&&snap.Locked(),"Contact acquires detent");Check(snap.ContactFaces({2,0,0}).size()==2,"Move snap exposes both highlighted faces");
    Check(Near(apply({12,0,0}).x,2),"Contact holds through inclusive +10 pixels at one pixel per unit");
    Check(Near(apply({12.01f,0,0}).x,12.01f)&&!snap.Locked(),"Contact releases beyond +10");Check(snap.ContactFaces({12.01f,0,0}).empty(),"Move highlight clears on release");
    Check(Near(apply({2.2f,0,0}).x,2.2f),"Reverse fine adjustment bypasses same contact");
    apply({1.9f,0,0});Check(Near(apply({2.2f,0,0}).x,2),"Cross back rearms contact");
    auto slide=apply({2.5f,.3f,0});Check(Near(slide.x,2)&&Near(slide.y,.3f),"Contact permits tangent sliding");
    Check(Near(apply({2.5f,2,0}).x,2.5f)&&!snap.Locked(),"Leaving face footprint releases detent");
    snap.Reset(scene,moving,{0,0,1});Check(Near(apply({2.5f,3,0}).x,2.5f),"Disjoint face footprints do not snap");
    snap.Reset(scene,moving,{0,0,1});Check(Near(apply({13,0,0}).x,13),"Large event beyond hold window does not stick");
    snap.Reset(scene,moving,{1,0,0});Check(Near(apply({2.5f,0,0}).x,2.5f),"Do not snap along forbidden plane normal");
    target.transform.position={-3,0,0};snap.Reset(scene,moving,{0,0,1});Check(Near(apply({-2.5f,0,0}).x,-2),"Negative direction contact");
    moving.transform.rotation.z=45;target.transform.rotation.z=45;Vector3 direction=glm::normalize(Vector3(1,1,0));target.transform.position=direction*3.f;
    snap.Reset(scene,moving,{0,0,1});auto rotated=apply(direction*2.5f);Check(glm::length(rotated-direction*2.f)<.002f,"Rotated parallel faces contact correctly");
    target.transform.rotation.z=20;snap.Reset(scene,moving,{0,0,1});Check(glm::length(apply(direction*2.5f)-direction*2.5f)<.002f,"Nonparallel faces are not falsely aligned");
    moving.transform=Transform{};target.transform=Transform{};target.transform.position={3,0,0};
    auto& group=scene.CreateGameObject("group");group.isGroup=true;moving.SetGroup(&group);
    snap.Reset(scene,group,{0,0,1});Check(Near(apply({2.5f,0,0}).x,2),"Group member faces snap as one object");
    target.activeSelf=false;snap.Reset(scene,group,{0,0,1});Check(Near(apply({2.5f,0,0}).x,2.5f),"Inactive targets excluded");
}
void TestSurfaceSnapZoom(){
    Scene scene;auto& moving=scene.CreateGameObject();moving.AddComponent<MeshRenderer>();auto& target=scene.CreateGameObject();target.AddComponent<MeshRenderer>();target.transform.position={3,0,0};
    for(bool perspective:{false,true})for(float zoom:{.5f,1.f,3.f})for(Vector2 size:{Vector2(800,600),Vector2(1600,900)}){
        Vector2 origin(73,51);auto view=glm::lookAt(Vector3(3,2,8)*zoom,Vector3(0),Vector3(0,1,0));
        auto projection=perspective?glm::perspective(glm::radians(50.f),size.x/size.y,.1f,100.f):glm::ortho(-5.f*zoom,5.f*zoom,-4.f*zoom,4.f*zoom,.1f,100.f);auto vp=projection*view;
        auto project=[&](Vector3 translation)->std::optional<Vector2>{auto p=GizmoMath::Project(translation,origin,size,vp);if(p)return Vector2(*p);return {};};
        auto contact=*project({2,0,0});auto direction=glm::normalize(*project({3,0,0})-contact);
        auto rawAt=[&](float pixels){auto ray=GizmoMath::ScreenRay(contact+direction*pixels,origin,size,vp);return *GizmoMath::PlaneIntersection(ray,Vector3(0),{0,0,1});};
        GizmoMath::SurfaceSnap snap;snap.Reset(scene,moving,{0,0,1});
        snap.Apply(rawAt(-2),project);
        Check(Near(snap.Apply(rawAt(4),project).x,2)&&snap.Locked(),"4px snaps at every zoom projection and viewport");
        Check(Near(snap.Apply(rawAt(10),project).x,2),"10px inclusive at every zoom");
        auto released=rawAt(10.1f);Check(glm::length(snap.Apply(released,project)-released)<.001f&&!snap.Locked(),"10.1px releases at every zoom");
        auto reverse=rawAt(2);Check(glm::length(snap.Apply(reverse,project)-reverse)<.001f,"Pixel snap preserves reverse fine control");
    }
}
void TestEditorCamera(){
    CameraWheelGesture wheel;
    auto press=wheel.Update({100,100},true,true,true,false);Check(press.captured&&!press.toggle&&glm::length(press.orbitDelta)==0,"Middle press waits for click versus drag");
    auto click=wheel.Update({102,100},false,false,true,false);Check(click.toggle&&glm::length(click.orbitDelta)==0,"Middle click toggles once despite small jitter");
    Check(!wheel.Update({102,100},false,false,true,false).toggle,"Released click not repeated");
    wheel.Update({100,100},true,true,true,false);auto drag=wheel.Update({100,90},false,true,true,false);Check(drag.captured&&!drag.toggle&&drag.orbitDelta==Vector2(0,-10),"Middle drag reports upward orbit delta");
    auto back=wheel.Update({100,100},false,true,true,false);Check(back.orbitDelta==Vector2(0,10),"Middle drag reports downward orbit delta");
    Check(!wheel.Update({100,100},false,false,true,false).toggle,"Drag returning to origin never toggles mode");
    wheel.Update({100,100},true,true,true,false);Check(!wheel.Update({100,100},false,false,true,true).toggle,"Cancelled gesture never toggles");
    Check(!wheel.Update({100,100},true,true,false,false).captured,"Middle click outside Scene ignored");

    EditorCamera camera;auto original=camera.position;auto forward=camera.Forward();camera.Move({0,0,1},.1f);Check(glm::length(camera.position-original-forward*.5f)<.001f,"W moves camera forward");
    camera.Move({0,0,-1},.1f);Check(glm::length(camera.position-original)<.001f,"S reverses camera movement");
    camera.Move({1,0,0},.1f);Check(glm::dot(camera.position-original,camera.Right())>.49f,"D strafes right");camera.Move({-1,0,0},.1f);
    camera.Move({0,1,0},.1f);Check(Near(camera.position.y,original.y+.5f),"E moves world up");camera.Move({0,-1,0},.1f);
    auto stationary=camera.position;camera.Look({100,-50});Check(camera.position==stationary&&camera.Forward()!=forward,"Look changes yaw pitch in place");
    camera.Look({0,-10000});Check(camera.pitch<=89&&std::isfinite(camera.Right().x),"Look avoids pole singularity");
    camera.pitch=-20;auto panStart=camera.position;auto normal=camera.Forward();camera.Pan({30,-20},600);auto delta=camera.position-panStart;
    Check(glm::length(delta)>.01f&&std::abs(glm::dot(delta,normal))<.001f,"Pan remains perpendicular to camera view");
    auto zoomStart=camera.position;auto zoomDirection=camera.Forward();auto yawBefore=camera.yaw,pitchBefore=camera.pitch;camera.Zoom(2);
    Check(glm::length(camera.position-zoomStart-zoomDirection*2.f)<.001f&&camera.yaw==yawBefore&&camera.pitch==pitchBefore,"Zoom changes distance without rotating");
    auto pivot=camera.position+camera.Forward()*8.f+camera.Right()*.4f;
    auto orbitBefore=camera.Get(800,600).GetViewMatrix()*Vector4(pivot,1);auto distance=glm::length(camera.position-pivot);
    camera.Orbit(pivot,{50,-30});auto orbitAfter=camera.Get(800,600).GetViewMatrix()*Vector4(pivot,1);
    Check(Near(glm::length(camera.position-pivot),distance)&&glm::length(orbitBefore-orbitAfter)<.001f,"Orbit preserves target screen position and distance without snapping");
    auto view=camera.Get(800,600).GetViewMatrix();auto eye=view*Vector4(camera.position,1);Check(glm::length(Vector3(eye))<.001f,"Render camera uses editor pose");
    EditorCamera diagonal;auto begin=diagonal.position;diagonal.Move({1,1,1},.1f);Check(Near(glm::length(diagonal.position-begin),.5f),"Diagonal speed normalized");
    PerspectiveCamera game(45,1,.1f,100);auto gameBefore=game.GetVPMatrix();camera.Move({0,0,1},.1f);camera.Get(800,600);Check(game.GetVPMatrix()==gameBefore,"Game camera remains independent");
}
void TestEdgeScaleSnap(){
    Scene scene;auto& object=scene.CreateGameObject();object.AddComponent<MeshRenderer>();auto& target=scene.CreateGameObject();target.AddComponent<MeshRenderer>();target.transform.position={2,0,0};
    GizmoMath::EdgeScaleSnap snap;
    for(float pixelsPerUnit:{20.f,100.f,400.f}){
        snap.Reset(scene,object,{.5f,.5f,0},{-.5f,-.5f,0},{1,1,0});
        auto project=[&](Vector3 p)->std::optional<Vector2>{return Vector2(p)*pixelsPerUnit;};
        auto result=snap.Apply({2+4/pixelsPerUnit,.7f,1},project);Check(Near(result.x,2)&&Near(result.y,.7f),"Edge contact snaps only touched dimension");
        result=snap.Apply({2+10/pixelsPerUnit,.8f,1},project);Check(Near(result.x,2)&&Near(result.y,.8f),"Edge holds inclusive 10px while other axis moves");
        float release=2+10.1f/pixelsPerUnit;Check(Near(snap.Apply({release,.8f,1},project).x,release),"Edge releases beyond 10px at all zoom levels");
        float reverse=2+2/pixelsPerUnit;Check(Near(snap.Apply({reverse,.8f,1},project).x,reverse),"Edge reverse fine control");
        snap.Apply({1.9f,.8f,1},project);Check(Near(snap.Apply({2+2/pixelsPerUnit,.8f,1},project).x,2),"Edge contact rearms");
    }
    auto& top=scene.CreateGameObject();top.AddComponent<MeshRenderer>();top.transform.position={0,2,0};
    auto project=[](Vector3 p)->std::optional<Vector2>{return Vector2(p)*100.f;};
    snap.Reset(scene,object,{.5f,.5f,0},{-.5f,-.5f,0},{1,1,0});auto both=snap.Apply({2.04f,2.04f,1},project);
    Check(Near(both.x,2)&&Near(both.y,2),"Two independent face contacts can hold together");Check(snap.ContactFaces(both).size()==4,"Two-axis resize exposes both contact pairs");
    Transform preview;GizmoMath::ScaleFromAnchor(preview,object.transform,{1,1,0},both,{-.5f,-.5f,0});
    Check(glm::length(Vector3(preview.GetMatrix()*Vector4(-.5f,-.5f,.5f,1))-Vector3(-.5f,-.5f,.5f))<.001f,"Snapped scale keeps opposite edge fixed");
    target.transform.position.y=10;top.activeSelf=false;snap.Reset(scene,object,{.5f,.5f,0},{-.5f,-.5f,0},{1,1,0});
    Check(Near(snap.Apply({2.04f,1,1},project).x,2.04f),"Resize snap rejects disjoint faces");
}
int main(){try{
    GizmoMath::RotationSnap snap;snap.Reset(84);
    Check(Near(snap.Apply(85),90),"Enter +90 detent");Check(Near(snap.Apply(90),90),"Hold center");Check(Near(snap.Apply(95),90),"Inclusive 95 boundary");Check(Near(snap.Apply(95.1f),95.1f),"Release beyond 95");Check(Near(snap.Apply(92),92),"Reverse fine adjustment");Check(Near(snap.Apply(90.1f),90.1f),"Reverse approaches freely");Check(Near(snap.Apply(90),90),"Center rearm");
    snap.Reset(-84);Check(Near(snap.Apply(-85),-90),"Negative entry");Check(Near(snap.Apply(-95),-90),"Negative boundary");Check(Near(snap.Apply(-96),-96),"Negative release");Check(Near(snap.Apply(-92),-92),"Negative fine reverse");
    snap.Reset(174);Check(Near(snap.Apply(176),180),"180 snap");snap.Reset(354);Check(Near(snap.Apply(356),360),"360 snap");
    for(float sign:{1.f,-1.f}){
        snap.Reset(sign*84);
        for(float center:{90.f,180.f,270.f,360.f,450.f}){
            Check(Near(snap.Apply(sign*(center-4)),sign*center)&&snap.Locked(),"Every quarter turn snaps in one gesture");
            Check(Near(snap.Apply(sign*(center+5)),sign*center),"Each quarter turn holds through five degrees");
            Check(Near(snap.Apply(sign*(center+6)),sign*(center+6))&&!snap.Locked(),"Each quarter turn releases");
            Check(Near(snap.Apply(sign*(center+2)),sign*(center+2))&&!snap.Locked(),"Released quarter turn permits reverse fine adjustment");
        }
        for(float center:{360.f,270.f,180.f,90.f,0.f}){
            Check(Near(snap.Apply(sign*(center+4)),sign*center)&&snap.Locked(),"Returning to earlier quarter turns rearms snap");
            Check(Near(snap.Apply(sign*(center-6)),sign*(center-6))&&!snap.Locked(),"Reverse quarter turn releases");
        }
    }
    Scene scene;auto& far=scene.CreateGameObject("far");far.transform.position={0,0,-4};far.AddComponent<MeshRenderer>();auto& near=scene.CreateGameObject("near");near.AddComponent<MeshRenderer>();
    GizmoMath::Ray ray{{0,0,5},{0,0,-1}};Check(GizmoMath::Pick(scene,ray)==near.GetID(),"Pick nearest");near.SetActive(false);Check(GizmoMath::Pick(scene,ray)==far.GetID(),"Skip inactive");
    near.SetActive(true);near.transform.localScale={0,1,1};Check(GizmoMath::Pick(scene,ray)==far.GetID(),"Skip singular transform");
    auto vp=glm::perspective(glm::radians(45.f),1.5f,.1f,100.f)*glm::lookAt(Vector3(0,0,5),Vector3(0),Vector3(0,1,0));
    auto screen=GizmoMath::Project(Vector3(0),{100,50},{900,600},vp);Check(screen&&Near(screen->x,550)&&Near(screen->y,350),"Viewport offset projection");auto screenRay=GizmoMath::ScreenRay({550,350},{100,50},{900,600},vp);Check(screenRay.direction.z<-.999f,"Unproject ray");
    Transform base;base.rotation={0,85,0};base.localScale={2,3,4};Vector3 pivot(.5f,0,0);auto fixed=Vector3(base.GetMatrix()*Vector4(pivot,1));auto rotated=base;GizmoMath::RotateFace(rotated,base,pivot,1,10);Check(Near(rotated.rotation.y,95),"Continuous Euler beyond 90");Check(glm::length(Vector3(rotated.GetMatrix()*Vector4(pivot,1))-fixed)<.001f,"Face pivot remains stationary");
    Transform scaled;GizmoMath::ScaleSelected(scaled,base,{1,1,0},2);Check(scaled.localScale==Vector3(4,6,4)&&scaled.position==base.position,"Edge scales two axes, origin fixed");
    GizmoMath::ScaleSelected(scaled,base,{1,1,1},-2);Check(Near(scaled.localScale.x,.04f),"Scale cannot cross zero");
    Transform anchored;Vector3 anchor(-.5f,-.5f,0);GizmoMath::ScaleFromAnchor(anchored,base,{1,1,0},2,anchor);
    Check(glm::length(Vector3(anchored.GetMatrix()*Vector4(anchor,1))-Vector3(base.GetMatrix()*Vector4(anchor,1)))<.001f,"Opposite edge anchor stays fixed");
    auto grabbed=Vector3(.5f,.5f,0);auto beforeGrab=Vector3(base.GetMatrix()*Vector4(grabbed,1)),fixedAnchor=Vector3(base.GetMatrix()*Vector4(anchor,1));
    Check(glm::length(Vector3(anchored.GetMatrix()*Vector4(grabbed,1))-(fixedAnchor+2.f*(beforeGrab-fixedAnchor)))<.001f,"Grabbed side expands away from fixed edge");
    Transform sheared=base;sheared.affine[2][0]=.4f;GizmoMath::ScaleFromAnchor(anchored,sheared,{1,1,0},.5f,anchor);
    for(float z:{-.5f,.5f}){auto end=Vector4(anchor.x,anchor.y,z,1);Check(glm::length(Vector3(anchored.GetMatrix()*end)-Vector3(sheared.GetMatrix()*end))<.001f,"Entire opposite edge stays fixed with shear");}
    auto edgeWorld=glm::scale(Matrix4(1),Vector3(2,3,1))*sheared.GetMatrix();Vector3 edgeHandle(.5f,.5f,0),edgeAnchor(-.5f,-.5f,0),edgeMask(1,1,0);
    auto edgeNormal=GizmoMath::FaceNormal(edgeWorld,2);auto at=[&](Vector3 local){auto point=Vector3(edgeWorld*Vector4(local,1));return GizmoMath::Ray{point+edgeNormal*10.f,-edgeNormal};};
    auto independent=GizmoMath::EdgeScaleFactors(edgeWorld,edgeHandle,edgeAnchor,edgeMask,at(edgeHandle),at(edgeHandle+Vector3(.4f,-.2f,0)));
    Check(independent&&Near(independent->x,1.4f)&&Near(independent->y,.8f)&&Near(independent->z,1),"Edge independently expands X and shrinks Y");
    auto oneAxis=GizmoMath::EdgeScaleFactors(edgeWorld,edgeHandle,edgeAnchor,edgeMask,at(edgeHandle),at(edgeHandle+Vector3(.3f,0,0)));
    Check(oneAxis&&Near(oneAxis->x,1.3f)&&Near(oneAxis->y,1),"Single-axis edge movement leaves other dimension unchanged");
    GizmoMath::ScaleFromAnchor(anchored,sheared,edgeMask,*independent,edgeAnchor);
    for(float z:{-.5f,.5f}){auto end=Vector4(edgeAnchor.x,edgeAnchor.y,z,1);Check(glm::length(Vector3(anchored.GetMatrix()*end)-Vector3(sheared.GetMatrix()*end))<.001f,"Independent resize preserves entire opposite edge");}
    auto negative=GizmoMath::EdgeScaleFactors(edgeWorld,edgeAnchor,edgeHandle,edgeMask,at(edgeAnchor),at(edgeAnchor-Vector3(.2f,.4f,0)));
    Check(negative&&Near(negative->x,1.2f)&&Near(negative->y,1.4f),"Negative-side handle has correct axis signs");
    auto parentScale=glm::scale(Matrix4(1),Vector3(2,3,4));GizmoMath::ScaleFromAnchor(anchored,base,{1,1,1},1.5f,Vector3(-.5f));
    Check(glm::length(Vector3(parentScale*anchored.GetMatrix()*Vector4(Vector3(-.5f),1))-Vector3(parentScale*base.GetMatrix()*Vector4(Vector3(-.5f),1)))<.001f,"Opposite corner fixed under scaled parent");
    ComponentRegistry registry;EditorSession editor(registry);auto id=editor.scene->CreateGameObject().GetID();auto before=SceneCodec(registry).Encode(*editor.scene);editor.scene->Find(id)->transform=rotated;editor.Record(before);editor.Undo();Check(Near(editor.scene->Find(id)->transform.rotation.y,0),"Gesture undo");editor.Redo();Check(Near(editor.scene->Find(id)->transform.rotation.y,95),"Gesture redo");
    auto plane=GizmoMath::PlaneIntersection({{0,0,5},{0,0,-1}},{0,0,.5f},{0,0,1});Check(plane&&Near(plane->z,.5f),"Ray-plane intersection");
    Check(!GizmoMath::PlaneIntersection({{0,0,5},{1,0,0}},{0,0,0},{0,0,1}),"Parallel plane has no unstable movement");
    auto shear=glm::scale(glm::rotate(Matrix4(1),.7f,Vector3(0,1,0)),Vector3(2,3,4));shear[1][0]+=.5f;
    auto normal=GizmoMath::FaceNormal(shear,2);Check(std::abs(glm::dot(normal,Vector3(shear[0])))<.001f&&std::abs(glm::dot(normal,Vector3(shear[1])))<.001f,"Face plane normal handles rotated scaled sheared parents");
    Check(Near(GizmoMath::HandleRadius(true),3.5f)&&Near(GizmoMath::HandleRadius(false),2.5f)&&Near(GizmoMath::HandleHitRadius(true),10)&&Near(GizmoMath::HandleHitRadius(false),8),"Half visual size, unchanged hit ranges");
    GizmoMath::ScreenOrbit orbit;orbit.Reset({100,100},{100,100});Check(Near(orbit.Apply({150,100}),0),"First radial motion establishes orbit reference");
    Check(Near(orbit.Apply({100,50}),90),"Screen counterclockwise quarter turn");Check(Near(orbit.Apply({50,100}),180),"Screen half turn");Check(Near(orbit.Apply({100,150}),270),"Orbit unwraps 180 boundary");Check(Near(orbit.Apply({150,100}),360),"Full orbit");Check(Near(orbit.Apply({100,150}),270),"Reverse orbit");
    Check(Near(orbit.Apply({100,100}),270)&&Near(orbit.Apply({150,100}),270),"Center crossing has no angle jump");
    Transform oblique;oblique.position={1,2,3};oblique.rotation={25,40,15};oblique.localScale={2,1,3};oblique.affine[1][0]=.3f;
    auto parent=glm::rotate(glm::scale(Matrix4(1),Vector3(2,3,1)),.3f,Vector3(0,1,0));auto beforeWorld=parent*oblique.GetMatrix();
    Vector3 endA(beforeWorld*Vector4(0,0,-.5f,1)),endB(beforeWorld*Vector4(0,0,.5f,1));auto result=GizmoMath::RotateWorldAxis(oblique,parent,(endA+endB)*.5f,glm::normalize(endB-endA),73);auto afterWorld=parent*result.GetMatrix();
    Check(glm::length(Vector3(afterWorld*Vector4(0,0,-.5f,1))-endA)<.002f&&glm::length(Vector3(afterWorld*Vector4(0,0,.5f,1))-endB)<.002f,"Both axis circles stay fixed under transformed parents");
    Scene picking;auto& back=picking.CreateGameObject("back");back.AddComponent<MeshRenderer>();auto& front=picking.CreateGameObject("front");front.AddComponent<MeshRenderer>();front.transform.position.z=2;
    auto& hidden=picking.CreateGameObject("collider only");hidden.AddComponent<BoxCollider>();hidden.transform.position.z=3;
    auto& group=picking.CreateGameObject("group");group.isGroup=true;front.SetGroup(&group);
    Check(GizmoMath::Pick(picking,{{0,0,5},{0,0,-1}},false,true)==front.GetID(),"Orbit picks front rendered member, not invisible collider or group bounds");
    TestEditorInput();
    TestSurfaceSnap();
    TestSurfaceSnapZoom();
    TestEditorCamera();
    TestEdgeScaleSnap();
    std::cout<<"Gizmo snap/picking/projection/pivot/undo tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
