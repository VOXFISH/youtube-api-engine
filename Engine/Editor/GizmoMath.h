#pragma once
#include "Rice/Benchmark.h"
#include "Scene/Components.h"
#include <optional>
namespace Rice::GizmoMath {
// Stateful detent: after release, reverse travel may approach the center freely.
class RotationSnap {
    float previous=0,target=0;bool locked=false,bypass=false;
public:
    void Reset(float angle){ RICE_BENCHMARK_FUNCTION(); previous=angle;locked=false;bypass=false;}
    bool Locked()const{ RICE_BENCHMARK_FUNCTION(); return locked;}
    float Apply(float angle){
        RICE_BENCHMARK_FUNCTION();
        float nearest=std::round(angle/90.f)*90.f;
        if(locked){if(std::abs(angle-target)<=5){previous=angle;return target;}locked=false;bypass=true;}
        // Fine adjustment bypass belongs only to the detent just released.
        // Entering another 90-degree segment rearms snapping in both directions.
        if(bypass){
            if(nearest!=target || ((previous-target)*(angle-target)<=0&&previous!=angle))bypass=false;
            else{previous=angle;return angle;}
        }
        if(!bypass&&angle!=previous){
            if(std::abs(angle-nearest)<=5){target=nearest;locked=true;previous=angle;return target;}}
        previous=angle;return angle;
    }
};
// Accumulate cursor orbit without jumps at +/-180 degrees or near the axis.
class ScreenOrbit {
    Vector2 center{0};float previous=0,total=0;bool ready=false;
public:
    void Reset(Vector2 origin,Vector2 mouse){ RICE_BENCHMARK_FUNCTION(); center=origin;total=0;ready=false;Apply(mouse);}
    float Apply(Vector2 mouse){ RICE_BENCHMARK_FUNCTION(); auto radial=mouse-center;if(glm::length(radial)<12.f){ready=false;return total;}
        float angle=glm::degrees(std::atan2(-radial.y,radial.x));
        if(ready)total+=std::remainder(angle-previous,360.f);
        previous=angle;ready=true;return total;
    }
};
inline Transform RotateWorldAxis(const Transform& start,const Matrix4& parent,Vector3 center,Vector3 axis,float degrees){
    RICE_BENCHMARK_FUNCTION();
    auto rotation=glm::translate(Matrix4(1),center)*glm::rotate(Matrix4(1),glm::radians(degrees),glm::normalize(axis))*glm::translate(Matrix4(1),-center);
    Transform result=start;result.SetMatrix(glm::inverse(parent)*rotation*parent*start.GetMatrix());return result;
}
struct Ray {Vector3 origin,direction;};
inline std::optional<Vector3> PlaneIntersection(const Ray& ray,Vector3 point,Vector3 normal){
    RICE_BENCHMARK_FUNCTION();
    float denominator=glm::dot(ray.direction,normal);
    if(std::abs(denominator)<1e-5f)return {};
    float distance=glm::dot(point-ray.origin,normal)/denominator;
    if(!std::isfinite(distance)||distance<0)return {};
    return ray.origin+distance*ray.direction;
}
inline Vector3 FaceNormal(const Matrix4& world,int axis){
    RICE_BENCHMARK_FUNCTION();
    Vector3 local(0);local[axis]=1;
    return glm::normalize(glm::transpose(glm::inverse(glm::mat3(world)))*local);
}
inline constexpr float HandleRadius(bool face){return face?3.5f:2.5f;}
inline constexpr float HandleHitRadius(bool face){return face?10.f:8.f;}
inline Ray ScreenRay(Vector2 pixel,Vector2 origin,Vector2 size,const Matrix4& vp){
    RICE_BENCHMARK_FUNCTION();
    Vector2 ndc=(pixel-origin)/size*2.f-Vector2(1);ndc.y=-ndc.y;auto inv=glm::inverse(vp);
    Vector4 near=inv*Vector4(ndc,-1,1),far=inv*Vector4(ndc,1,1);near/=near.w;far/=far.w;
    return {Vector3(near),glm::normalize(Vector3(far-near))};
}
inline std::optional<Vector3> Project(Vector3 world,Vector2 origin,Vector2 size,const Matrix4& vp){
    RICE_BENCHMARK_FUNCTION();
    auto p=vp*Vector4(world,1);if(p.w<=0.f)return {};p/=p.w;if(p.z < -1 || p.z > 1)return {};
    return Vector3(origin.x+(p.x+1)*.5f*size.x,origin.y+(1-p.y)*.5f*size.y,p.z);
}
inline std::pair<Vector3,Vector3> PrimitiveBounds(GameObject& obj){
    RICE_BENCHMARK_FUNCTION();
    if(auto* mesh=obj.GetComponent<MeshRenderer>()) {
        if(mesh->shape==int(BasicShape::Custom))return mesh->custom.Bounds();
        if(mesh->shape==int(BasicShape::Circle)||mesh->shape==int(BasicShape::Rectangle))
            return {{-.5f,-.5f,-.025f},{.5f,.5f,.025f}};
        return {Vector3(-.5f),Vector3(.5f)};
    }
    if(obj.GetComponent<SpriteRenderer>())return {{-.5f,-.5f,-.025f},{.5f,.5f,.025f}};
    if(auto* c=obj.GetComponent<BoxCollider>())return {c->center-glm::max(glm::abs(c->size),Vector3(.05f))*.5f,c->center+glm::max(glm::abs(c->size),Vector3(.05f))*.5f};
    return {Vector3(-.5f),Vector3(.5f)};
}
inline std::pair<Vector3,Vector3> SpaceBounds(GameObject& obj,const Matrix4& space){
    RICE_BENCHMARK_FUNCTION();
    Vector3 low(1e30f),high(-1e30f);bool any=false;
    if(obj.isGroup)for(auto& child:obj.GetScene().GetObjects())if(!child->IsPendingDestroy()&&child->GetHierarchyParent()==&obj){auto [a,b]=SpaceBounds(*child,space);low=glm::min(low,a);high=glm::max(high,b);any=true;}
    if(!any||obj.GetComponent<MeshRenderer>()||obj.GetComponent<SpriteRenderer>()||obj.GetComponent<BoxCollider>()){
        auto [a,b]=PrimitiveBounds(obj);auto matrix=space*obj.GetWorldMatrix();
        for(int i=0;i<8;++i){Vector3 corner;for(int k=0;k<3;++k)corner[k]=(i&(1<<k))?b[k]:a[k];auto point=Vector3(matrix*Vector4(corner,1));low=glm::min(low,point);high=glm::max(high,point);}
    }
    return {low,high};
}
inline std::pair<Vector3,Vector3> Bounds(GameObject& obj){
    RICE_BENCHMARK_FUNCTION();
    if(obj.isGroup&&std::abs(glm::determinant(obj.GetWorldMatrix()))>1e-8f)return SpaceBounds(obj,glm::inverse(obj.GetWorldMatrix()));
    return PrimitiveBounds(obj);
}
inline std::optional<float> Hit(const Ray& ray,const Matrix4& world,Vector3 low,Vector3 high){
    RICE_BENCHMARK_FUNCTION();
    if(std::abs(glm::determinant(world))<1e-8f)return {};
    auto inv=glm::inverse(world);Vector3 o(inv*Vector4(ray.origin,1)),d(inv*Vector4(ray.direction,0));float enter=0,exit=1e30f;
    for(int axis=0;axis<3;++axis){if(std::abs(d[axis])<1e-7f){if(o[axis]<low[axis]||o[axis]>high[axis])return {};continue;}
        float a=(low[axis]-o[axis])/d[axis],b=(high[axis]-o[axis])/d[axis];if(a>b)std::swap(a,b);enter=std::max(enter,a);exit=std::min(exit,b);if(enter>exit)return {};}
    return enter;
}
inline EntityID Pick(Scene& scene,const Ray& ray,bool selectGroups=true,bool renderedOnly=false){ RICE_BENCHMARK_FUNCTION(); EntityID selected=0;float closest=1e30f;for(auto& obj:scene.GetObjects())if(obj->IsActive()){
    auto* mesh=obj->GetComponent<MeshRenderer>();auto* sprite=obj->GetComponent<SpriteRenderer>();auto* collider=obj->GetComponent<BoxCollider>();
    if(renderedOnly&&!(mesh&&mesh->enabled)&&!(sprite&&sprite->enabled))continue;
    if(!(selectGroups&&obj->isGroup)&&!(mesh&&mesh->enabled)&&!(sprite&&sprite->enabled)&&!(collider&&collider->enabled))continue;
    auto [low,high]=renderedOnly?PrimitiveBounds(*obj):Bounds(*obj);auto hit=Hit(ray,obj->GetWorldMatrix(),low,high);
    if(hit&&mesh&&mesh->enabled&&mesh->shape==int(BasicShape::Custom)&&!mesh->custom.Empty()&&!obj->isGroup){
        auto inverse=glm::inverse(obj->GetWorldMatrix());hit=mesh->Intersection().Hit(Vector3(inverse*Vector4(ray.origin,1)),Vector3(inverse*Vector4(ray.direction,0)));
    }
    if(hit&&*hit<closest){closest=*hit;selected=obj->GetID();}}
    if(selectGroups)if(auto* obj=scene.Find(selected))for(auto* p=obj->GetHierarchyParent();p;p=p->GetHierarchyParent())if(p->isGroup)selected=p->GetID();
    return selected;
}
inline Vector3 ContinuousEuler(Quaternion rotation,Vector3 reference){
    RICE_BENCHMARK_FUNCTION();
    auto a=glm::degrees(glm::eulerAngles(rotation));auto b=Vector3(a.x+180,180-a.y,a.z+180);
    for(int i=0;i<3;++i){a[i]+=360*std::round((reference[i]-a[i])/360);b[i]+=360*std::round((reference[i]-b[i])/360);}
    return glm::length(a-reference)<=glm::length(b-reference)?a:b;
}
inline void ScaleSelected(Transform& t,const Transform& start,Vector3 mask,float factor){
    RICE_BENCHMARK_FUNCTION();
    t=start;factor=std::max(.02f,factor);for(int i=0;i<3;++i)if(mask[i])t.localScale[i]=start.localScale[i]*factor;
}
inline void ScaleFromAnchor(Transform& t,const Transform& start,Vector3 mask,Vector3 factors,Vector3 anchor){
    RICE_BENCHMARK_FUNCTION();
    Vector3 scale(1);
    for(int i=0;i<3;++i)if(mask[i])scale[i]=std::max(.02f,factors[i]);
    auto local=glm::translate(Matrix4(1),anchor)*glm::scale(Matrix4(1),scale)*glm::translate(Matrix4(1),-anchor);
    t=start;t.SetMatrix(start.GetMatrix()*local);
    t.rotation=ContinuousEuler(Quaternion(glm::radians(t.rotation)),start.rotation);
}
inline void ScaleFromAnchor(Transform& t,const Transform& start,Vector3 mask,float factor,Vector3 anchor){ RICE_BENCHMARK_FUNCTION(); ScaleFromAnchor(t,start,mask,Vector3(factor),anchor);}
inline std::optional<Vector3> EdgeScaleFactors(const Matrix4& world,Vector3 handle,Vector3 anchor,Vector3 mask,const Ray& initial,const Ray& current){
    RICE_BENCHMARK_FUNCTION();
    if(std::abs(glm::determinant(world))<1e-8f)return {};
    int edgeAxis=0;for(int i=0;i<3;++i)if(!mask[i])edgeAxis=i;
    auto point=Vector3(world*Vector4(handle,1)),normal=FaceNormal(world,edgeAxis);
    auto a=PlaneIntersection(initial,point,normal),b=PlaneIntersection(current,point,normal);if(!a||!b)return {};
    auto delta=Vector3(glm::inverse(world)*Vector4(*b-*a,0));Vector3 factors(1);
    for(int i=0;i<3;++i)if(mask[i]){float span=handle[i]-anchor[i];if(std::abs(span)<1e-7f)return {};factors[i]=std::max(.02f,1+delta[i]/span);}
    return factors;
}
inline void RotateFace(Transform& t,const Transform& start,Vector3 pivot,int axis,float degrees){
    RICE_BENCHMARK_FUNCTION();
    Vector3 normal(0);normal[axis]=1;
    Quaternion before(glm::radians(start.rotation));Quaternion after=glm::normalize(before*glm::angleAxis(glm::radians(degrees),normal));
    Vector3 reference=start.rotation;reference[axis]+=degrees;
    t.rotation=ContinuousEuler(after,reference);
    // Keep the clicked face-center stationary in parent coordinates.
    auto localPivot=Vector3(start.affine*Vector4(pivot,1));
    t.position=start.position+before*(start.localScale*localPivot)-after*(start.localScale*localPivot);
}
}
