#pragma once

#include "PolygonColliderMesh.h"
#include <array>
#include <optional>

namespace Rice {

enum class ColliderKind { Box, Sphere, Polygon };

struct Collider : Component {
    explicit Collider(ColliderKind kind):kind(kind){}
    const ColliderKind kind;
    Vector3 center{0};
    bool isTrigger=false,overlapping=false;
    virtual std::pair<Vector3,Vector3> GetBounds() const=0;
};

struct BoxCollider : Collider {
    BoxCollider():Collider(ColliderKind::Box){}
    Vector3 size{1};
    mutable bool boundsCached=false;
    mutable Matrix4 boundsWorld{1};
    mutable Vector3 boundsCenter{0},boundsSize{0};
    mutable std::pair<Vector3,Vector3> cachedBounds;
    std::pair<Vector3,Vector3> GetBounds() const override {
        RICE_BENCHMARK_FUNCTION();
        auto matrix=GetGameObject().GetWorldMatrix();
        if(GameObject::WorldCacheEnabled()&&boundsCached&&boundsWorld==matrix&&boundsCenter==center&&boundsSize==size)return cachedBounds;
        Vector3 low(std::numeric_limits<float>::max()),high(-std::numeric_limits<float>::max());
        for(int i=0;i<8;++i){
            Vector3 corner=center+glm::abs(size)*Vector3(i&1?.5f:-.5f,i&2?.5f:-.5f,i&4?.5f:-.5f);
            Vector3 world(matrix*Vector4(corner,1));low=glm::min(low,world);high=glm::max(high,world);
        }
        if(GameObject::WorldCacheEnabled()){boundsCached=true;boundsWorld=matrix;boundsCenter=center;boundsSize=size;cachedBounds={low,high};}
        return {low,high};
    }
};

struct SphereCollider : Collider {
    SphereCollider():Collider(ColliderKind::Sphere){}
    float radius=.5f;
    std::pair<Vector3,Vector3> GetBounds() const override {
        auto world=GetGameObject().GetWorldMatrix();
        auto origin=Vector3(world*Vector4(center,1));
        float scale=std::max({glm::length(Vector3(world[0])),glm::length(Vector3(world[1])),glm::length(Vector3(world[2]))});
        float extent=std::max(radius,0.f)*scale;
        return {origin-extent,origin+extent};
    }
};

struct PolygonCollider : Collider {
    PolygonCollider():Collider(ColliderKind::Polygon){}
    mutable PolygonColliderMesh mesh;
    bool Prepare() const {
        auto& object=GetGameObject();
        return mesh.Prepare(object.GetComponent<MeshRenderer>(),glm::translate(object.GetWorldMatrix(),center));
    }
    std::pair<Vector3,Vector3> GetBounds() const override {
        if(Prepare()){auto bounds=mesh.Bounds();return {bounds.low,bounds.high};}
        auto origin=Vector3(GetGameObject().GetWorldMatrix()*Vector4(center,1));return {origin,origin};
    }
};

template<class F> void ForEachCollider(GameObject& object,F&& visit){
    if(auto* box=object.GetComponent<BoxCollider>())visit(*box);
    if(auto* sphere=object.GetComponent<SphereCollider>())visit(*sphere);
    if(auto* polygon=object.GetComponent<PolygonCollider>())visit(*polygon);
}

inline std::vector<Collider*> ActiveColliders(Scene& scene){
    std::vector<Collider*> result;
    for(const auto& object:scene.GetObjects())ForEachCollider(*object,[&](Collider& collider){
        collider.overlapping=false;
        if(!collider.enabled||!object->IsActive())return;
        if(collider.kind==ColliderKind::Polygon&&!static_cast<const PolygonCollider&>(collider).Prepare())return;
        result.push_back(&collider);
    });
    return result;
}

inline SpatialBounds ColliderBounds(const Collider& collider){
    auto [low,high]=collider.GetBounds();return {low,high};
}

namespace CollisionGeometry {
using Triangle=std::array<Vector3,3>;
struct Contact {Vector3 normal;float depth;}; // Moves the first shape away from the second.
inline Vector3 Center(const SpatialBounds& box){return (box.low+box.high)*.5f;}
inline Vector3 Extent(const SpatialBounds& box){return (box.high-box.low)*.5f;}
inline SpatialBounds Bounds(const Triangle& t){return {glm::min(t[0],glm::min(t[1],t[2])),glm::max(t[0],glm::max(t[1],t[2]))};}
inline Vector3 FaceNormal(const Triangle& t){return glm::cross(t[1]-t[0],t[2]-t[0]);}
inline float LengthSquared(Vector3 v){return glm::dot(v,v);}

inline Vector3 ClosestOnSegment(Vector3 point,Vector3 a,Vector3 b){
    auto edge=b-a;float squared=LengthSquared(edge);
    return squared>0 ? a+glm::clamp(glm::dot(point-a,edge)/squared,0.f,1.f)*edge : a;
}
inline Vector3 ClosestOnTriangle(Vector3 p,const Triangle& t){
    auto normal=FaceNormal(t);
    if(float squared=LengthSquared(normal);squared>0){
        auto projected=p-normal*(glm::dot(p-t[0],normal)/squared);
        bool inside=true;
        for(int i=0;i<3;++i)inside&=glm::dot(glm::cross(t[(i+1)%3]-t[i],projected-t[i]),normal)>=0.f;
        if(inside)return projected;
    }
    auto nearest=ClosestOnSegment(p,t[0],t[1]);
    for(int i=1;i<3;++i){
        auto candidate=ClosestOnSegment(p,t[i],t[(i+1)%3]);
        if(LengthSquared(p-candidate)<LengthSquared(p-nearest))nearest=candidate;
    }
    return nearest;
}

inline std::pair<float,float> Project(const Triangle& t,Vector3 axis,Vector3 origin){
    float a=glm::dot(t[0]-origin,axis),b=glm::dot(t[1]-origin,axis),c=glm::dot(t[2]-origin,axis);
    return {std::min({a,b,c}),std::max({a,b,c})};
}

// Separating axes for two triangles, including in-plane edge normals for coplanar triangles.
inline bool IntersectTriangles(const Triangle& a,const Triangle& b){
    auto na=FaceNormal(a),nb=FaceNormal(b);
    if(LengthSquared(na)==0||LengthSquared(nb)==0)return false;
    auto separated=[&](Vector3 axis){
        auto [al,ah]=Project(a,axis,a[0]);auto [bl,bh]=Project(b,axis,a[0]);
        float tolerance=8.f*std::numeric_limits<float>::epsilon()*std::max({std::abs(al),std::abs(ah),std::abs(bl),std::abs(bh)});
        return ah<bl-tolerance || bh<al-tolerance;
    };
    if(separated(na)||separated(nb))return false;
    for(int i=0;i<3;++i){
        auto ea=a[(i+1)%3]-a[i],eb=b[(i+1)%3]-b[i];
        if(separated(glm::cross(na,ea))||separated(glm::cross(nb,eb)))return false;
        for(int j=0;j<3;++j)if(separated(glm::cross(ea,b[(j+1)%3]-b[j])))return false;
    }
    return true;
}

inline std::optional<Contact> BoxTriangle(const SpatialBounds& box,const Triangle& t){
    auto center=Center(box),extent=Extent(box);
    Contact hit{{0,1,0},std::numeric_limits<float>::max()};
    auto intersects=[&](Vector3 axis){
        float length=glm::length(axis);
        if(length==0.f)return true;
        axis/=length;
        auto [low,high]=Project(t,axis,center);float radius=glm::dot(glm::abs(axis),extent);
        float positive=high+radius,negative=radius-low;
        if(positive<0 || negative<0)return false;
        float depth=std::min(positive,negative);
        if(depth<hit.depth)hit={positive<negative?axis:-axis,depth};
        return true;
    };
    auto normal=FaceNormal(t);
    if(LengthSquared(normal)==0||!intersects(normal))return {};
    for(int i=0;i<3;++i){
        Vector3 axis(0);axis[i]=1;
        if(!intersects(axis))return {};
        for(int j=0;j<3;++j)if(!intersects(glm::cross(axis,t[(j+1)%3]-t[j])))return {};
    }
    return hit;
}

inline Contact BoxBox(const SpatialBounds& a,const SpatialBounds& b){
    // Preserve the established AABB response, including its deterministic axis tie break.
    Vector3 depth=glm::min(a.high,b.high)-glm::max(a.low,b.low);
    int axis=depth.x<depth.y?0:1;if(depth.z<depth[axis])axis=2;
    Vector3 normal(0);normal[axis]=(a.low[axis]+a.high[axis]<b.low[axis]+b.high[axis])?-1.f:1.f;
    return {normal,depth[axis]};
}

inline std::optional<Contact> SphereSphere(const SpatialBounds& a,const SpatialBounds& b){
    auto delta=Center(a)-Center(b);float distance=glm::length(delta);
    float depth=Extent(a).x+Extent(b).x-distance;
    if(depth<0)return {};
    return Contact{distance>0?delta/distance:Vector3(0,1,0),depth};
}
inline std::optional<Contact> SphereBox(const SpatialBounds& sphere,const SpatialBounds& box){
    auto center=Center(sphere),nearest=glm::clamp(center,box.low,box.high),delta=center-nearest;
    float distance=glm::length(delta),radius=Extent(sphere).x;
    if(distance>radius)return {};
    if(distance>0)return Contact{delta/distance,radius-distance};
    auto low=center-box.low,high=box.high-center;
    auto gap=glm::min(low,high);
    int axis=gap.x<gap.y?0:1;if(gap.z<gap[axis])axis=2;
    Vector3 normal(0);normal[axis]=low[axis]<high[axis]?-1.f:1.f;
    return Contact{normal,radius+gap[axis]};
}

inline Contact FromSurface(Vector3 point,Vector3 nearest,const Triangle& t,bool inside,float radius){
    Vector3 offset=inside?nearest-point:point-nearest;
    float distance=glm::length(offset);
    Vector3 normal;
    if(distance>0)normal=offset/distance;
    else{normal=FaceNormal(t);float length=glm::length(normal);normal=length>0?normal/length:Vector3(0,1,0);}
    return {normal,inside?radius+distance:std::max(0.f,radius-distance)};
}

inline Contact ExitSurface(const PolygonColliderMesh& mesh,Vector3 point){
    float best=std::numeric_limits<float>::max();
    Triangle face{};Vector3 nearest=point;
    // Only needed for complete containment without any crossing surface.
    for(size_t i=0;i<mesh.TriangleCount();++i){
        auto t=mesh.Triangle(i);
        if(LengthSquared(FaceNormal(t))==0)continue;
        auto candidate=ClosestOnTriangle(point,t);float squared=LengthSquared(candidate-point);
        if(squared<best){best=squared;face=t;nearest=candidate;}
    }
    return FromSurface(point,nearest,face,true,0.f);
}

inline std::optional<Contact> PrimitiveMesh(bool sphere,const SpatialBounds& bounds,const PolygonColliderMesh& mesh,bool response){
    auto center=Center(bounds);
    std::optional<Contact> best;
    Triangle nearestFace{};Vector3 nearest{};
    float bestSquared=std::numeric_limits<float>::max(),radius=Extent(bounds).x;
    mesh.QueryWorldBounds(bounds,[&](size_t i){
        if(!response&&best)return;
        auto t=mesh.Triangle(i);
        if(sphere){
            if(LengthSquared(FaceNormal(t))==0)return;
            auto candidate=ClosestOnTriangle(center,t);float squared=LengthSquared(candidate-center);
            if(squared<=radius*radius&&squared<bestSquared){bestSquared=squared;nearest=candidate;nearestFace=t;}
        }else if(auto hit=BoxTriangle(bounds,t);hit&&(!best||hit->depth>best->depth))best=hit;
        if(!response&&bestSquared!=std::numeric_limits<float>::max())best=Contact{{0,1,0},0};
    });
    if(!response&&best)return best;
    if(bestSquared!=std::numeric_limits<float>::max()){
        bool inside=mesh.Contains(center);
        return FromSurface(center,nearest,nearestFace,inside,radius);
    }
    if(!mesh.Contains(center))return best;
    if(!response)return Contact{{0,1,0},0};
    auto exit=ExitSurface(mesh,center);
    exit.depth+=sphere?radius:glm::dot(glm::abs(exit.normal),Extent(bounds));
    return exit;
}

// Test a candidate separation in world space without moving either live object.
inline bool OverlapsAfterTranslation(const PolygonColliderMesh& a,const PolygonColliderMesh& b,Vector3 delta){
    auto moved=a.Bounds(),other=b.Bounds();moved.low+=delta;moved.high+=delta;
    if(!moved.Overlaps(other))return false;
    SpatialBounds query{glm::max(moved.low,other.low)-delta,glm::min(moved.high,other.high)-delta};
    bool overlaps=false;
    a.QueryWorldBounds(query,[&](size_t i){
        if(overlaps)return;
        auto t=a.Triangle(i);for(auto& point:t)point+=delta;
        b.QueryWorldBounds(Bounds(t),[&](size_t j){
            if(!overlaps)overlaps=IntersectTriangles(t,b.Triangle(j));
        });
    });
    if(!overlaps)a.ForEachIslandPoint([&](Vector3 point){if(!overlaps)overlaps=b.Contains(point+delta);});
    if(!overlaps)b.ForEachIslandPoint([&](Vector3 point){if(!overlaps)overlaps=a.Contains(point-delta);});
    return overlaps;
}

inline std::optional<Contact> MeshMesh(const PolygonColliderMesh& a,const PolygonColliderMesh& b,bool response){
    auto ab=a.Bounds(),bb=b.Bounds();
    auto overlap=SpatialBounds{glm::max(ab.low,bb.low),glm::min(ab.high,bb.high)};
    std::optional<Contact> best;
    // Retain distinct surface and edge directions at the actual intersections.
    // Bound the response manifold even for thousands of nearly coplanar faces.
    std::array<Vector3,32> axes{};
    size_t axisCount=0,intersections=0;
    auto addAxis=[&](Vector3 axis)->size_t{
        float length=glm::length(axis);if(length==0)return axes.size();
        axis/=length;
        for(size_t i=0;i<axisCount;++i)if(std::abs(glm::dot(axes[i],axis))>.99999f)return i;
        if(axisCount==axes.size())return axisCount;
        axes[axisCount]=axis;return axisCount++;
    };
    struct Plane {size_t axis;float position;bool fromA;};
    std::array<Plane,16> planes{};size_t planeCount=0;
    auto origin=Center(ab);
    const float tolerance=1e-5f*(1+glm::length(Extent(ab))+glm::length(Extent(bb)));
    auto addFace=[&](const Triangle& t,bool fromA){
        size_t axis=addAxis(FaceNormal(t));
        if(axis==axes.size()||planeCount==planes.size())return;
        float position=glm::dot(t[0]-origin,axes[axis]);
        for(size_t i=0;i<planeCount;++i){
            const auto& p=planes[i];
            if(p.axis==axis&&p.fromA==fromA&&std::abs(p.position-position)<=tolerance)return;
        }
        planes[planeCount++]={axis,position,fromA};
    };
    auto enough=[&]{return (!response&&intersections)||intersections>=64;};
    a.QueryWorldBounds(overlap,[&](size_t i){
        if(enough())return;
        auto first=a.Triangle(i);
        b.QueryWorldBounds(Bounds(first),[&](size_t j){
            if(enough())return;
            auto second=b.Triangle(j);
            if(!IntersectTriangles(first,second))return;
            ++intersections;
            if(!response)return;
            addFace(first,true);addFace(second,false);
            // Reserve space for additional face normals from the rest of the patch.
            if(axisCount<axes.size()/2)for(int u=0;u<3;++u)for(int v=0;v<3;++v)
                addAxis(glm::cross(first[(u+1)%3]-first[u],second[(v+1)%3]-second[v]));
        });
    });
    if(intersections){
        if(!response)return Contact{{0,1,0},0};
        std::optional<Contact> local;
        for(size_t i=0;i<axisCount;++i){
            auto axis=axes[i];
            auto rangeA=a.Project(axis,origin),rangeB=b.Project(axis,origin);
            auto [al,ah]=rangeA;auto [bl,bh]=rangeB;
            float positive=bh-al,negative=ah-bl;
            float depth=std::max(0.f,std::min(positive,negative));
            if(!best||depth<best->depth)best=Contact{positive<negative?axis:-axis,depth};
            // A surface inside the mesh's projection may border a notch or an
            // empty space between shells. Try moving just past that surface.
            for(size_t j=0;j<planeCount;++j){
                auto p=planes[j];if(p.axis!=i)continue;
                auto surface=p.fromA?rangeA:rangeB,body=p.fromA?rangeB:rangeA;
                if(p.position<=surface.first+tolerance||p.position>=surface.second-tolerance)continue;
                float plus=p.position-body.first,minus=body.second-p.position;
                float localDepth=std::min(plus,minus);
                if(localDepth<=tolerance||(local&&localDepth>=local->depth))continue;
                Vector3 direction=plus<minus?axis:-axis;
                local=Contact{p.fromA?-direction:direction,localDepth};
            }
        }
        if(local&&local->depth+tolerance<best->depth&&
           !OverlapsAfterTranslation(a,b,local->normal*(local->depth+tolerance)))return local;
        return best;
    }
    // Nonintersecting surfaces can still enclose a complete connected component.
    auto enclosed=[&](const PolygonColliderMesh& inner,const PolygonColliderMesh& outer,bool reverse){
        inner.ForEachIslandPoint([&](Vector3 point){
            if(best||!outer.Contains(point))return;
            if(!response){best=Contact{{0,1,0},0};return;}
            auto hit=ExitSurface(outer,point);
            hit.depth-=inner.Project(hit.normal,point).first;
            if(reverse)hit.normal=-hit.normal;
            best=hit;
        });
    };
    enclosed(a,b,false);if(!best)enclosed(b,a,true);
    return best;
}

inline std::optional<Contact> Intersect(const Collider& a,const SpatialBounds& ab,const Collider& b,const SpatialBounds& bb,bool response){
    if(!ab.Overlaps(bb))return {};
    if(a.kind==ColliderKind::Box&&b.kind==ColliderKind::Box)return BoxBox(ab,bb);
    if(a.kind==ColliderKind::Sphere&&b.kind==ColliderKind::Sphere)return SphereSphere(ab,bb);
    if(a.kind==ColliderKind::Polygon&&b.kind==ColliderKind::Polygon)
        return MeshMesh(static_cast<const PolygonCollider&>(a).mesh,static_cast<const PolygonCollider&>(b).mesh,response);
    if(b.kind==ColliderKind::Polygon)
        return PrimitiveMesh(a.kind==ColliderKind::Sphere,ab,static_cast<const PolygonCollider&>(b).mesh,response);
    if(a.kind==ColliderKind::Sphere)return SphereBox(ab,bb);
    auto swapped=Intersect(b,bb,a,ab,response);
    if(swapped)swapped->normal=-swapped->normal;
    return swapped;
}
}

class CollisionSystem {
public:
    static size_t Update(Scene& scene,bool useBVH=true,size_t* comparisons=nullptr){
        RICE_BENCHMARK_FUNCTION();
        if(comparisons)*comparisons=0;
        auto colliders=ActiveColliders(scene);
        std::vector<SpatialBounds> bounds;bounds.reserve(colliders.size());
        for(auto* collider:colliders)bounds.push_back(ColliderBounds(*collider));
        size_t count=0;
        auto check=[&](size_t i,size_t j){
            auto& a=*colliders[i];auto& b=*colliders[j];
            if(&a.GetGameObject()==&b.GetGameObject())return;
            if(comparisons)++*comparisons;
            if(CollisionGeometry::Intersect(a,bounds[i],b,bounds[j],false)){a.overlapping=b.overlapping=true;++count;}
        };
        if(useBVH){
            SpatialBVH tree;tree.Build(bounds);
            for(size_t i=0;i<colliders.size();++i)tree.QueryOverlaps(bounds[i],[&](size_t j){if(j>i)check(i,j);});
        }else for(size_t i=0;i<colliders.size();++i)for(size_t j=i+1;j<colliders.size();++j)check(i,j);
        return count;
    }
};
}
