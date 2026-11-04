#pragma once
#include "GizmoMath.h"
#include "EditorEditUI.h"
#include <imgui.h>

namespace Rice {
inline bool EditFiniteVector(const char* label,Vector3& value,float speed,const BeforeEditorEdit& beforeEdit={}){
    return EditValue(value,beforeEdit,[&](Vector3& next){
        bool changed=ImGui::DragFloat3(label,&next.x,speed,0,0,"%.3f");
        return changed&&std::isfinite(next.x)&&std::isfinite(next.y)&&std::isfinite(next.z);
    });
}
inline void DrawPivotFields(GameObject& object,const BeforeEditorEdit& beforeEdit={}){
    EditFiniteVector("Pivot (X / Y / Z)",object.transform.pivot,.01f,beforeEdit);
    ImGui::SetItemTooltip("Fractions of local bounds: 0 = left / bottom / back, 1 = right / top / front. Values outside 0..1 are allowed.");
}
inline void DrawAnchorFields(Anchor& anchor,Scene& scene,EntityID parent,bool enabledField=true,const BeforeEditorEdit& beforeEdit={}){
    if(enabledField)EditValue(anchor.enabled,beforeEdit,[](bool& next){return ImGui::Checkbox("Enabled",&next);});
    EditValue(anchor.showInEditor,beforeEdit,[](bool& next){return ImGui::Checkbox("On in Shape Editor",&next);});
    auto* owner=scene.Find(parent);auto* child=scene.Find(anchor.child);
    if(child&&child->GetParent()!=owner)child=nullptr;
    if(ImGui::BeginCombo("Child",child?child->name.c_str():"none")){
        if(ImGui::Selectable("none",!child)&&anchor.child){if(beforeEdit)beforeEdit();anchor.child=0;}
        if(owner)for(auto& candidate:scene.GetObjects())if(!candidate->IsPendingDestroy()&&candidate->GetParent()==owner){
            ImGui::PushID(candidate.get());if(ImGui::Selectable(candidate->name.c_str(),child==candidate.get())&&anchor.child!=candidate->GetID()){if(beforeEdit)beforeEdit();anchor.child=candidate->GetID();}ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    EditFiniteVector("Translation",anchor.transform.position,.02f,beforeEdit);
    EditFiniteVector("Rotation",anchor.transform.rotation,1.f,beforeEdit);
    EditFiniteVector("Scale",anchor.transform.localScale,.02f,beforeEdit);
    ImGui::SetItemTooltip("The child's pivot attaches here. Rotation and scale are inherited by the child.");
}
inline void DrawCameraFollowerFields(CameraFollower& follower,Scene& scene,EntityID owner,const BeforeEditorEdit& beforeEdit={}){
    EditValue(follower.enabled,beforeEdit,[](bool& next){return ImGui::Checkbox("Enabled",&next);});
    EditValue(follower.showInEditor,beforeEdit,[](bool& next){return ImGui::Checkbox("On in Shape Editor",&next);});
    auto* current=scene.Find(follower.camera);
    if(ImGui::BeginCombo("Follow camera",current?current->name.c_str():"none")){
        if(ImGui::Selectable("none",!current)&&follower.camera){if(beforeEdit)beforeEdit();follower.camera=0;}
        for(auto& candidate:scene.GetObjects())if(candidate->GetID()!=owner&&!candidate->IsPendingDestroy()&&candidate->GetComponent<CameraComponent>()){
            ImGui::PushID(candidate.get());if(ImGui::Selectable(candidate->name.c_str(),candidate.get()==current)&&follower.camera!=candidate->GetID()){if(beforeEdit)beforeEdit();follower.camera=candidate->GetID();}ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    EditFiniteVector("Translation",follower.transform.position,.02f,beforeEdit);
    EditFiniteVector("Rotation",follower.transform.rotation,1.f,beforeEdit);
    EditFiniteVector("Scale",follower.transform.localScale,.02f,beforeEdit);
    ImGui::TextWrapped("Pivot-relative frame aligned to the camera during Play. Camera forward is -Z.");
}
inline void DrawCameraFields(CameraComponent& camera,const BeforeEditorEdit& beforeEdit={}){
    EditValue(camera.enabled,beforeEdit,[](bool& next){return ImGui::Checkbox("Enabled",&next);});
    EditValue(camera.primary,beforeEdit,[](bool& next){return ImGui::Checkbox("Primary (Game view)",&next);});
    EditValue(camera.fieldOfView,beforeEdit,[](float& next){return ImGui::SliderFloat("Field of view",&next,1,179);});
    if(EditValue(camera.nearClip,beforeEdit,[](float& next){bool changed=ImGui::DragFloat("Near clip",&next,.01f,.001f,1000);if(changed)next=std::max(.001f,next);return changed;}))camera.farClip=std::max(camera.nearClip+.001f,camera.farClip);
    EditValue(camera.farClip,beforeEdit,[&](float& next){bool changed=ImGui::DragFloat("Far clip",&next,1,.002f,100000);if(changed)next=std::max(camera.nearClip+.001f,next);return changed;});
    ImGui::PushID(&camera);
    auto* state=ImGui::GetStateStorage();Vector3 target(state->GetFloat(ImGui::GetID("lookX")),state->GetFloat(ImGui::GetID("lookY")),state->GetFloat(ImGui::GetID("lookZ")));
    if(EditFiniteVector("LookAt (world)",target,.05f)){state->SetFloat(ImGui::GetID("lookX"),target.x);state->SetFloat(ImGui::GetID("lookY"),target.y);state->SetFloat(ImGui::GetID("lookZ"),target.z);}
    if(ImGui::Button("Apply LookAt")){if(beforeEdit)beforeEdit();camera.LookAt(target);}
    ImGui::PopID();
}
inline void DrawCameraIcon(ImDrawList* draw,const Matrix4& frame,Vector2 origin,Vector2 size,const Matrix4& vp,bool enabled,bool selected=false){
    auto color=selected?IM_COL32(255,220,35,255):enabled?IM_COL32(70,210,255,255):IM_COL32(130,130,140,255);
    auto line=[&](Vector3 a,Vector3 b,ImU32 tint){auto p=GizmoMath::Project(Vector3(frame*Vector4(a,1)),origin,size,vp),q=GizmoMath::Project(Vector3(frame*Vector4(b,1)),origin,size,vp);if(p&&q)draw->AddLine({p->x,p->y},{q->x,q->y},tint,2.f);};
    Vector3 corners[]={{-.16f,-.11f,-.3f},{.16f,-.11f,-.3f},{.16f,.11f,-.3f},{-.16f,.11f,-.3f}};
    for(int i=0;i<4;++i){line(Vector3(0),corners[i],color);line(corners[i],corners[(i+1)%4],color);}
    line({-.1f,.11f,-.3f},{0,.19f,-.3f},color);line({0,.19f,-.3f},{.1f,.11f,-.3f},color);

}
inline void DrawAnchorArrow(ImDrawList* draw,Vector3 position,Vector3 direction,Vector2 origin,Vector2 size,const Matrix4& vp){
    auto start=GizmoMath::Project(position,origin,size,vp);if(!start)return;
    auto end=GizmoMath::Project(position+direction,origin,size,vp);
    Vector2 delta=end?Vector2(*end-*start):Vector2(0,-1);if(glm::length(delta)<.01f)delta={0,-1};else delta=glm::normalize(delta);
    Vector2 from(*start),to=from+delta*30.f,side(-delta.y,delta.x);
    auto point=[](Vector2 p){return ImVec2(p.x,p.y);};auto yellow=IM_COL32(255,220,35,255);
    draw->AddLine(point(from),point(to),yellow,2.f);
    draw->AddTriangleFilled(point(to),point(to-delta*7.f+side*4.f),point(to-delta*7.f-side*4.f),yellow);
}
inline bool DrawSceneAnchor(GameObject& object,ImVec2 origin,ImVec2 size,const Matrix4& vp,bool hovered){
    auto* anchor=object.GetComponent<Anchor>();if(!anchor||!anchor->enabled)return false;
    auto matrix=object.GetPivotMatrix()*anchor->transform.GetMatrix();Vector3 position(matrix[3]);
    auto screen=GizmoMath::Project(position,{origin.x,origin.y},{size.x,size.y},vp);if(!screen)return false;
    ImGui::PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);auto* draw=ImGui::GetWindowDrawList();
    auto direction=object.GetWorldRotation()*Quaternion(glm::radians(anchor->transform.rotation))*Vector3(0,0,1);
    DrawAnchorArrow(draw,position,direction,{origin.x,origin.y},{size.x,size.y},vp);
    auto mouse=ImGui::GetIO().MousePos;bool hot=hovered&&glm::length(Vector2(mouse.x,mouse.y)-Vector2(*screen))<=10.f;
    draw->AddCircleFilled({screen->x,screen->y},hot?6.f:4.f,IM_COL32(255,220,35,255),16);ImGui::PopClipRect();
    if(hot)ImGui::SetTooltip("Anchor - edit Translation / Rotation / Scale in Inspector");return hot;
}
}
