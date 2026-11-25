#pragma once
#include "Rice/Benchmark.h"
#include "GizmoMath.h"
#include "SurfaceSnap.h"
#include "Project/SceneCodec.h"
#include <imgui.h>
#include <cstdio>
namespace Rice {
class TransformGizmo {
    enum class Mode{None,Scale,Move,Rotate};
    Mode mode=Mode::None;EntityID dragging=0;Transform start;
    Vector2 mouseStart{0},direction{0};Vector3 pivot{0},worldAxis{0},scaleMask{0},scaleAnchor{0};
    Matrix4 scaleStartWorld{1};GizmoMath::Ray scaleStartRay{};
    Vector3 planePoint{0},planeNormal{0},planeStart{0};bool rightOwned=false;
    int axis=0,button=0;float faceSign=1,pixelsPerUnit=1;
    GizmoMath::ScreenOrbit orbit;Matrix4 rotationParent{1};Vector3 rotationCenter{0},rotationEndA{0},rotationEndB{0};float rotationSign=1,rotationAngle=0;
    Vector3 snapTranslation{0},snapFactors{1};
    GizmoMath::SurfaceSnap surfaceSnap;GizmoMath::EdgeScaleSnap edgeSnap;
    GizmoMath::RotationSnap snap;std::optional<EditorSession::EditSnapshot> snapshot;
    struct Handle{Vector3 local,screen;bool face=false;int axis=0;float sign=1;Vector3 mask{0};};
    static ImVec2 Point(Vector3 p){ RICE_BENCHMARK_FUNCTION(); return {p.x,p.y};}
    void Finish(EditorSession& editor,bool cancel){ RICE_BENCHMARK_FUNCTION(); if(auto* obj=editor.scene->Find(dragging)){if(cancel)editor.PreviewTransform(dragging,start);else if(snapshot)editor.Record(*snapshot);}snapshot.reset();mode=Mode::None;dragging=0;}
public:
    EntityID DraggingObject()const{ RICE_BENCHMARK_FUNCTION(); return dragging;}
    bool consumed=false,handleHovered=false;
    void BeginFrame(){ RICE_BENCHMARK_FUNCTION(); handleHovered=false;consumed=Active()||rightOwned;if(!ImGui::IsMouseDown(1))rightOwned=false;}

    bool Active()const{ RICE_BENCHMARK_FUNCTION(); return mode!=Mode::None;}
    void Cancel(EditorSession& editor){ RICE_BENCHMARK_FUNCTION(); if(Active())Finish(editor,true);}
    void Commit(EditorSession& editor){if(Active())Finish(editor,false);}
    void Draw(EditorSession& editor,ImVec2 origin,ImVec2 extent,const Matrix4& vp,EntityID target=0,bool canvasHovered=false){
        RICE_BENCHMARK_FUNCTION();
        Vector2 mouse(ImGui::GetIO().MousePos.x,ImGui::GetIO().MousePos.y),viewOrigin(origin.x,origin.y),size(extent.x,extent.y);
        bool hovered=canvasHovered;
        if(editor.playing){Cancel(editor);return;}
        auto* obj=editor.scene->Find(target?target:editor.selected);
        if(Active()&&obj&&obj->GetID()==dragging){
            if(ImGui::IsKeyPressed(ImGuiKey_Escape)||!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)){Finish(editor,true);}
            else if(!ImGui::IsMouseDown(button)){Finish(editor,false);}
            else {
                auto delta=mouse-mouseStart;
                Transform preview=start;bool changed=true;
                if(mode==Mode::Move){auto point=GizmoMath::PlaneIntersection(GizmoMath::ScreenRay(mouse,viewOrigin,size,vp),planePoint,planeNormal);
                    if(point){auto parent=obj->GetParentMatrix();if(std::abs(glm::determinant(parent))>1e-8f)preview.position+=Vector3(glm::inverse(parent)*Vector4(snapTranslation=surfaceSnap.Apply(*point-planeStart,[&](Vector3 translation)->std::optional<Vector2>{ RICE_BENCHMARK_FUNCTION(); auto screen=GizmoMath::Project(planeStart+translation,viewOrigin,size,vp);if(!screen)return {};return Vector2(*screen);}),0));else changed=false;}else changed=false;
                }
                if(mode==Mode::Scale){
                    if(scaleMask.x+scaleMask.y+scaleMask.z==2){
                        auto factors=GizmoMath::EdgeScaleFactors(scaleStartWorld,pivot,scaleAnchor,scaleMask,scaleStartRay,GizmoMath::ScreenRay(mouse,viewOrigin,size,vp));
                        if(factors){auto corrected=edgeSnap.Apply(*factors,[&](Vector3 worldPoint)->std::optional<Vector2>{ RICE_BENCHMARK_FUNCTION(); auto screen=GizmoMath::Project(worldPoint,viewOrigin,size,vp);if(screen)return Vector2(*screen);return {};});snapFactors=corrected;GizmoMath::ScaleFromAnchor(preview,start,scaleMask,corrected,scaleAnchor-obj->GetLocalPivot());}else changed=false;
                    }else{float factor=std::max(.02f,1+glm::dot(delta,direction)/pixelsPerUnit);GizmoMath::ScaleFromAnchor(preview,start,scaleMask,factor,scaleAnchor-obj->GetLocalPivot());}
                }
                if(mode==Mode::Rotate){float raw=start.rotation[axis]+orbit.Apply(mouse)*rotationSign*faceSign;float angle=snap.Apply(raw);rotationAngle=angle;
                    preview=GizmoMath::RotateWorldAxis(start,rotationParent,rotationCenter,worldAxis,(angle-start.rotation[axis])*faceSign);
                    auto reference=start.rotation;reference[axis]=angle;preview.rotation=GizmoMath::ContinuousEuler(Quaternion(glm::radians(preview.rotation)),reference);
                }
                if(changed)editor.PreviewTransform(dragging,preview);
            }
        }
        std::vector<Handle> handles;std::optional<Handle> hot;
        if(obj&&obj->IsActive()&&std::abs(glm::determinant(obj->GetWorldMatrix()))>1e-8f){
            auto [low,high]=GizmoMath::Bounds(*obj);Vector3 center=(low+high)*.5f;auto world=obj->GetWorldMatrix();
            std::optional<Vector3> corners[8];auto* draw=ImGui::GetWindowDrawList();
            ImGui::PushClipRect(origin,{origin.x+extent.x,origin.y+extent.y},true);
            auto add=[&](Vector3 local,bool face,int a,float sign,Vector3 mask){ RICE_BENCHMARK_FUNCTION(); auto p=GizmoMath::Project(Vector3(world*Vector4(local,1)),viewOrigin,size,vp);if(p)handles.push_back({local,*p,face,a,sign,mask});};
            for(int i=0;i<8;++i){Vector3 p;for(int a=0;a<3;++a)p[a]=(i&(1<<a))?high[a]:low[a];corners[i]=GizmoMath::Project(Vector3(world*Vector4(p,1)),viewOrigin,size,vp);add(p,false,0,1,Vector3(1));}
            for(int i=0;i<8;++i)for(int a=0;a<3;++a)if(!(i&(1<<a))){int j=i|(1<<a);if(corners[i]&&corners[j])draw->AddLine(Point(*corners[i]),Point(*corners[j]),IM_COL32(100,205,255,160),1.5f);
                Vector3 p,mask(1);for(int k=0;k<3;++k)p[k]=k==a?center[k]:((i&(1<<k))?high[k]:low[k]);mask[a]=0;add(p,false,0,1,mask);}
            for(int a=0;a<3;++a)for(int sign:{-1,1}){Vector3 p=center;p[a]=sign>0?high[a]:low[a];add(p,true,a,float(sign),Vector3(0));}
            // Draw distant handles first, so nearer handles win overlapping hit tests.
            std::sort(handles.begin(),handles.end(),[](auto& a,auto& b){ RICE_BENCHMARK_FUNCTION(); return a.screen.z>b.screen.z;});
            for(auto& h:handles){float radius=GizmoMath::HandleRadius(h.face);bool under=hovered&&glm::length(mouse-Vector2(h.screen))<GizmoMath::HandleHitRadius(h.face);if(under)hot=h;
                ImU32 color=under?IM_COL32(255,220,70,255):h.face?(h.axis==0?IM_COL32(255,105,105,255):h.axis==1?IM_COL32(105,240,140,255):IM_COL32(100,170,255,255)):IM_COL32(200,235,255,235);
                if(h.face){draw->AddCircleFilled(Point(h.screen),radius,IM_COL32(20,25,30,180));draw->AddCircle(Point(h.screen),radius,color,20,2);}
                else{draw->AddRectFilled({h.screen.x-radius,h.screen.y-radius},{h.screen.x+radius,h.screen.y+radius},IM_COL32(20,25,30,180));draw->AddRect({h.screen.x-radius,h.screen.y-radius},{h.screen.x+radius,h.screen.y+radius},color);}
            }
            ImGui::PopClipRect();
            if(hot&&!Active())ImGui::SetTooltip(hot->face?"Left drag: move on face plane | Right drag: orbit around the axis joining opposite circles\nDrag outward, then circle clockwise/counterclockwise":"Drag: resize edge axes independently; opposite side stays fixed");
            if(hot)handleHovered=true;
            bool canBegin=!Active()&&!consumed;
            if(hot&&ImGui::IsMouseClicked(1)){consumed=true;rightOwned=true;}
            if(canBegin&&hot&&!ImGui::GetIO().KeyShift&&(ImGui::IsMouseClicked(0)||(hot->face&&ImGui::IsMouseClicked(1)))){
                consumed=true;editor.selected=obj->GetID();
                start=obj->transform;dragging=obj->GetID();mouseStart=mouse;pivot=hot->local;axis=hot->axis;faceSign=hot->sign;scaleMask=hot->mask;scaleAnchor=center*2.f-hot->local;button=ImGui::IsMouseClicked(1)?1:0;
                planePoint=Vector3(world*Vector4(pivot,1));planeNormal=GizmoMath::FaceNormal(world,axis);
                if(hot->face&&button==0){auto hit=GizmoMath::PlaneIntersection(GizmoMath::ScreenRay(mouse,viewOrigin,size,vp),planePoint,planeNormal);if(!hit){dragging=0;return;}planeStart=*hit;surfaceSnap.Reset(*editor.scene,*obj,planeNormal);}
                scaleStartWorld=world;scaleStartRay=GizmoMath::ScreenRay(mouse,viewOrigin,size,vp);
                if(!hot->face&&scaleMask.x+scaleMask.y+scaleMask.z==2)edgeSnap.Reset(*editor.scene,*obj,pivot,scaleAnchor,scaleMask);
                snapshot=editor.CaptureObject(dragging);mode=hot->face?(button?Mode::Rotate:Mode::Move):Mode::Scale;snap.Reset(start.rotation[axis]);rotationAngle=start.rotation[axis];
                Vector3 axisLocal(0);axisLocal[axis]=faceSign;worldAxis=Vector3(world*Vector4(axisLocal,0));float length=glm::length(worldAxis);worldAxis=length>1e-6f?worldAxis/length:Vector3(0,1,0);
                if(mode==Mode::Rotate){
                    rotationParent=obj->GetParentMatrix()*glm::translate(Matrix4(1),obj->GetLocalPivot());rotationCenter=Vector3(world*Vector4(center,1));
                    rotationEndA=Vector3(world*Vector4(pivot,1));rotationEndB=rotationCenter*2.f-rotationEndA;
                    rotationSign=glm::dot(worldAxis,GizmoMath::ScreenRay(Vector2(hot->screen),viewOrigin,size,vp).direction)<=0?1.f:-1.f;
                    orbit.Reset(Vector2(hot->screen),mouse);
                }
                auto startScreen=GizmoMath::Project(Vector3(world*Vector4(pivot,1)),viewOrigin,size,vp);
                auto endScreen=GizmoMath::Project(Vector3(world*Vector4(pivot,1))+worldAxis,viewOrigin,size,vp);
                Vector2 projected=startScreen&&endScreen?Vector2(*endScreen-*startScreen):Vector2(0,-1);
                if(mode==Mode::Scale){auto anchorScreen=GizmoMath::Project(Vector3(world*Vector4(scaleAnchor,1)),viewOrigin,size,vp);projected=startScreen&&anchorScreen?Vector2(*startScreen-*anchorScreen):Vector2(0,-80);}
                pixelsPerUnit=glm::length(projected);direction=pixelsPerUnit>=3?projected/pixelsPerUnit:Vector2(0,-1);pixelsPerUnit=std::max(pixelsPerUnit,mode==Mode::Scale?40.f:20.f);
            }
            if(dragging==obj->GetID()){
                std::vector<GizmoMath::SurfaceSnap::Face> faces;
                if(mode==Mode::Move)faces=surfaceSnap.ContactFaces(snapTranslation);
                else if(mode==Mode::Scale&&scaleMask.x+scaleMask.y+scaleMask.z==2)faces=edgeSnap.ContactFaces(snapFactors);
                if(!faces.empty()){
                    constexpr ImU32 highlight=IM_COL32(80,240,170,255);
                    ImGui::PushClipRect(origin,{origin.x+extent.x,origin.y+extent.y},true);
                    for(auto& face:faces){std::vector<ImVec2> points;
                        for(auto corner:face.corners){auto screen=GizmoMath::Project(corner,viewOrigin,size,vp);if(screen)points.push_back(Point(*screen));}
                        if(points.size()==4){float area=0;for(int i=0;i<4;++i)area+=points[i].x*points[(i+1)%4].y-points[(i+1)%4].x*points[i].y;
                            if(area<0)std::reverse(points.begin(),points.end());
                            draw->AddConvexPolyFilled(points.data(),4,IM_COL32(80,240,170,45));draw->AddPolyline(points.data(),4,highlight,ImDrawFlags_Closed,2.f);}
                    }
                    auto screen=GizmoMath::Project(Vector3(obj->GetWorldMatrix()*Vector4(pivot,1)),viewOrigin,size,vp);
                    if(screen){float radius=GizmoMath::HandleRadius(mode==Mode::Move);
                        if(mode==Mode::Move)draw->AddCircleFilled(Point(*screen),radius,highlight,20);
                        else draw->AddRectFilled({screen->x-radius,screen->y-radius},{screen->x+radius,screen->y+radius},highlight);
                        draw->AddText({screen->x+10,screen->y-18},highlight,"CONTACT SNAP");
                    }
                    ImGui::PopClipRect();
                }
            }
            // Draw the captured rotation axis last, above geometry and ordinary
            // handles. Its endpoints stay fixed even when group bounds change.
            if(mode==Mode::Rotate&&dragging==obj->GetID()){
                auto a=GizmoMath::Project(rotationEndA,viewOrigin,size,vp),b=GizmoMath::Project(rotationEndB,viewOrigin,size,vp);
                const ImU32 highlight=snap.Locked()?IM_COL32(80,240,170,255):IM_COL32(255,195,45,255);
                constexpr ImU32 outline=IM_COL32(25,20,10,230);
                ImGui::PushClipRect(origin,{origin.x+extent.x,origin.y+extent.y},true);
                if(a&&b){draw->AddLine(Point(*a),Point(*b),outline,4.f);draw->AddLine(Point(*a),Point(*b),highlight,2.f);}
                for(auto endpoint:{a,b})if(endpoint){draw->AddCircleFilled(Point(*endpoint),GizmoMath::HandleRadius(true),highlight,20);draw->AddCircle(Point(*endpoint),GizmoMath::HandleRadius(true),outline,20,1.f);}
                // The endpoints can coincide in a head-on view; keep the axis
                // identifiable without moving or enlarging either handle.
                if(a){ImVec2 label{a->x+10,a->y-18};char text[96];std::snprintf(text,sizeof(text),"%c rotation: %.1f deg%s",'X'+axis,rotationAngle,snap.Locked()?" [SNAP]":"");draw->AddText({label.x+1,label.y+1},outline,text);draw->AddText(label,highlight,text);}
                ImGui::PopClipRect();
            }

        }

    }
};
}
