#pragma once
#include "Rice/Benchmark.h"
#include "GizmoMath.h"
#include <array>
#include <functional>
namespace Rice::GizmoMath {
// One-sided contact detent. Geometry is frozen at gesture start; only raw
// translation advances the state, so a held preview cannot accumulate error.
class SurfaceSnap {
public:
    struct Face {Vector3 normal,center;std::array<Vector3,4> corners;};
    struct Contact {Face moving,target;float gap;bool bypass=false;};
private:
    std::vector<Contact> contacts;
    Vector3 previous{0},travel{0};int locked=-1;
public:
    static std::vector<Face> Faces(GameObject& obj){
        RICE_BENCHMARK_FUNCTION();
        auto [low,high]=PrimitiveBounds(obj);auto world=obj.GetWorldMatrix();std::vector<Face> result;
        for(int axis=0;axis<3;++axis)for(int sign:{-1,1}){
            Face face;face.normal=FaceNormal(world,axis)*float(sign);Vector3 center=(low+high)*.5f;center[axis]=sign>0?high[axis]:low[axis];face.center=Vector3(world*Vector4(center,1));
            int u=(axis+1)%3,v=(axis+2)%3,index=0;
            for(auto uv:{Vector2(0,0),Vector2(1,0),Vector2(1,1),Vector2(0,1)}){auto point=center;point[u]=uv.x?high[u]:low[u];point[v]=uv.y?high[v]:low[v];face.corners[index++]=Vector3(world*Vector4(point,1));}
            result.push_back(face);
        }return result;
    }
    static bool Geometry(GameObject& obj){
        RICE_BENCHMARK_FUNCTION();
        auto* mesh=obj.GetComponent<MeshRenderer>();auto* sprite=obj.GetComponent<SpriteRenderer>();auto* collider=obj.GetComponent<BoxCollider>();
        return (mesh&&mesh->enabled)||(sprite&&sprite->enabled)||(collider&&collider->enabled);
    }
    static bool Overlap(const Contact& contact,Vector3 translation){
        RICE_BENCHMARK_FUNCTION();
        // SAT on both quadrilateral edge sets: unlike world AABBs this rejects
        // unrelated faces and works with rotated/nonuniformly scaled parents.
        for(auto* face:{&contact.moving,&contact.target})for(int i=0;i<4;++i){
            auto axis=glm::cross(contact.moving.normal,face->corners[(i+1)%4]-face->corners[i]);
            if(glm::length(axis)<1e-7f)continue;axis=glm::normalize(axis);
            float aMin=1e30f,aMax=-1e30f,bMin=1e30f,bMax=-1e30f;
            for(int j=0;j<4;++j){float a=glm::dot(contact.moving.corners[j]+translation,axis),b=glm::dot(contact.target.corners[j],axis);aMin=std::min(aMin,a);aMax=std::max(aMax,a);bMin=std::min(bMin,b);bMax=std::max(bMax,b);}
            if(std::min(aMax,bMax)-std::max(aMin,bMin)<=1e-5f)return false;
        }return true;
    }
public:
    static constexpr float HoldPixels=10.f;
    using ScreenProjection=std::function<std::optional<Vector2>(Vector3)>;
    void Reset(Scene& scene,GameObject& selected,Vector3 planeNormal){
        RICE_BENCHMARK_FUNCTION();
        contacts.clear();previous=Vector3(0);locked=-1;
        std::vector<Face> moving,targets;
        for(auto& obj:scene.GetObjects())if(obj->IsActive()&&Geometry(*obj)&&std::abs(glm::determinant(obj->GetWorldMatrix()))>1e-8f){
            bool moves=obj->DependsOn(&selected);
            if(!moves&&selected.DependsOn(obj.get()))continue;
            auto faces=Faces(*obj);auto& output=moves?moving:targets;output.insert(output.end(),faces.begin(),faces.end());
        }
        for(auto& a:moving){if(glm::length(a.normal-planeNormal*glm::dot(a.normal,planeNormal))<1e-5f)continue;
            for(auto& b:targets)if(glm::dot(a.normal,b.normal)<-.99999f){float gap=glm::dot(b.center-a.center,a.normal);if(gap>=-1e-5f)contacts.push_back({a,b,std::max(0.f,gap)});}}
    }
    Vector3 Apply(Vector3 raw,const ScreenProjection& project){
        RICE_BENCHMARK_FUNCTION();
        // Compare the pointer's uncorrected position with its contact-corrected
        // position in viewport pixels, using this frame's camera and viewport.
        auto withinPixels=[&](Vector3 corrected){ RICE_BENCHMARK_FUNCTION(); auto a=project(raw),b=project(corrected);return a&&b&&glm::length(*a-*b)<=HoldPixels+.0005f;};
        auto last=previous;previous=raw;
        if(locked>=0){auto& contact=contacts[locked];float amount=(glm::dot(raw,contact.moving.normal)-contact.gap)/glm::dot(travel,contact.moving.normal);auto corrected=raw-travel*amount;
            if(amount>=-1e-5f&&withinPixels(corrected)&&Overlap(contact,corrected))return corrected;
            contact.bypass=true;locked=-1;
        }
        auto movement=raw-last;if(glm::length(movement)<1e-7f)return raw;
        auto direction=glm::normalize(movement);float earliest=2;int best=-1;Vector3 result=raw;
        for(int i=0;i<static_cast<int>(contacts.size());++i){auto& contact=contacts[i];auto n=contact.moving.normal;
            float before=glm::dot(last,n)-contact.gap,after=glm::dot(raw,n)-contact.gap;
            if(contact.bypass){if(after<=0)contact.bypass=false;continue;}
            float speed=glm::dot(direction,n);if(speed<=1e-6f||before>1e-5f||after<0)continue;
            float amount=after/speed;auto corrected=raw-direction*amount;
            if(!withinPixels(corrected)){contact.bypass=true;continue;}
            float fraction=std::max(0.f,-before)/glm::dot(movement,n);
            if(fraction<earliest&&Overlap(contact,corrected)){earliest=fraction;best=i;result=corrected;}
        }
        if(best>=0){locked=best;travel=direction;}return result;
    }
    bool Locked()const{ RICE_BENCHMARK_FUNCTION(); return locked>=0;}
    std::vector<Face> ContactFaces(Vector3 translation)const{
        RICE_BENCHMARK_FUNCTION();
        if(!Locked())return {};auto moving=contacts[locked].moving;
        moving.center+=translation;for(auto& corner:moving.corners)corner+=translation;
        return {moving,contacts[locked].target};
    }
};
}

namespace Rice::GizmoMath {
// Each resize axis owns its detent independently. All measurements use the
// original geometry and raw factors, never the previously snapped preview.
class EdgeScaleSnap {
    struct Candidate {SurfaceSnap::Face local,target;Vector3 normal;int axis;float coefficient;bool bypass=false;};
    std::vector<Candidate> candidates;Matrix4 world{1};Vector3 anchor{0},handle{0},previous{1};
    int locked[3]{-1,-1,-1};float direction[3]{1,1,1};
    SurfaceSnap::Face Scaled(const Candidate& c,Vector3 factors)const{
        RICE_BENCHMARK_FUNCTION();
        auto result=c.local;
        auto point=[&](Vector3 p){ RICE_BENCHMARK_FUNCTION(); return Vector3(world*Vector4(anchor+(p-anchor)*factors,1));};
        result.center=point(result.center);for(auto& corner:result.corners)corner=point(corner);
        result.normal=c.normal;return result;
    }
    float Penetration(const Candidate& c,Vector3 factors)const{ RICE_BENCHMARK_FUNCTION(); return glm::dot(Scaled(c,factors).center-c.target.center,c.normal);}
    bool ContactValid(const Candidate& c,Vector3 factors)const{
        RICE_BENCHMARK_FUNCTION();
        auto face=Scaled(c,factors);auto cross=glm::cross(face.corners[1]-face.corners[0],face.corners[3]-face.corners[0]);
        if(glm::length(cross)<1e-7f||std::abs(glm::dot(glm::normalize(cross),c.target.normal))<.99999f)return false;
        return SurfaceSnap::Overlap({face,c.target,0},Vector3(0));
    }
public:
    using Projection=SurfaceSnap::ScreenProjection;
    bool Locked()const{ RICE_BENCHMARK_FUNCTION(); return locked[0]>=0||locked[1]>=0||locked[2]>=0;}
    std::vector<SurfaceSnap::Face> ContactFaces(Vector3 factors)const{
        RICE_BENCHMARK_FUNCTION();
        std::vector<SurfaceSnap::Face> faces;
        for(auto index:locked)if(index>=0){const auto& c=candidates[index];if(ContactValid(c,factors)){faces.push_back(Scaled(c,factors));faces.push_back(c.target);}}
        return faces;
    }
    void Reset(Scene& scene,GameObject& selected,Vector3 selectedHandle,Vector3 fixedAnchor,Vector3 mask){
        RICE_BENCHMARK_FUNCTION();
        candidates.clear();world=selected.GetWorldMatrix();auto inverse=glm::inverse(world);handle=selectedHandle;anchor=fixedAnchor;previous=Vector3(1);for(auto& value:locked)value=-1;
        std::vector<SurfaceSnap::Face> moving,targets;
        for(auto& obj:scene.GetObjects())if(obj->IsActive()&&SurfaceSnap::Geometry(*obj)&&std::abs(glm::determinant(obj->GetWorldMatrix()))>1e-8f){
            bool moves=obj->DependsOn(&selected);if(!moves&&selected.DependsOn(obj.get()))continue;
            auto faces=SurfaceSnap::Faces(*obj);auto& output=moves?moving:targets;output.insert(output.end(),faces.begin(),faces.end());
        }
        for(auto& a:moving)for(auto& b:targets)if(glm::dot(a.normal,b.normal)<-.99999f&&glm::dot(b.center-a.center,a.normal)>=-1e-5f){
            auto local=a;local.center=Vector3(inverse*Vector4(a.center,1));for(auto& p:local.corners)p=Vector3(inverse*Vector4(p,1));
            for(int axis=0;axis<3;++axis)if(mask[axis]){float coefficient=glm::dot(Vector3(world[axis])*(local.center[axis]-anchor[axis]),a.normal);
                if(std::abs(coefficient)>1e-6f)candidates.push_back({local,b,a.normal,axis,coefficient});}
        }
    }
    Vector3 Apply(Vector3 raw,const Projection& project){
        RICE_BENCHMARK_FUNCTION();
        auto last=previous;previous=raw;auto result=raw;
        auto screen=[&](Vector3 factors){ RICE_BENCHMARK_FUNCTION(); return project(Vector3(world*Vector4(anchor+(handle-anchor)*factors,1)));};
        auto close=[&](Vector3 a,Vector3 b){ RICE_BENCHMARK_FUNCTION(); auto x=screen(a),y=screen(b);return x&&y&&glm::length(*x-*y)<=SurfaceSnap::HoldPixels+.0005f;};
        for(int axis=0;axis<3;++axis){
            if(locked[axis]>=0){auto& c=candidates[locked[axis]];auto corrected=result;corrected[axis]-=Penetration(c,result)/c.coefficient;
                if(corrected[axis]>=.02f&&(raw[axis]-corrected[axis])*direction[axis]>=-1e-5f&&close(result,corrected)&&ContactValid(c,corrected)){result=corrected;continue;}
                c.bypass=true;locked[axis]=-1;
            }
            float best=2;int chosen=-1;auto bestResult=result;
            for(int i=0;i<static_cast<int>(candidates.size());++i){auto& c=candidates[i];if(c.axis!=axis)continue;
                float before=Penetration(c,last),after=Penetration(c,raw);
                if(c.bypass){if(after<=0)c.bypass=false;continue;}
                if(before>1e-5f||after<0||after-before<=1e-7f||std::abs(raw[axis]-last[axis])<1e-7f)continue;
                auto corrected=result;corrected[axis]-=Penetration(c,result)/c.coefficient;
                if(corrected[axis]<.02f||!close(result,corrected)){c.bypass=true;continue;}
                float fraction=std::max(0.f,-before)/(after-before);
                if(fraction<best&&ContactValid(c,corrected)){best=fraction;chosen=i;bestResult=corrected;}
            }
            if(chosen>=0){locked[axis]=chosen;direction[axis]=raw[axis]>last[axis]?1.f:-1.f;result=bestResult;}
        }return result;
    }
};
}
