#pragma once
#include "ShapeMesh.h"
#include "SpatialBVH.h"
#include <optional>

namespace Rice {
// Cached triangle BVH: visibility checks stay responsive on sphere meshes.
class MeshIntersection {
    struct Triangle {Vector3 a,b,c;};
    std::vector<Triangle> triangles;
    SpatialBVH tree;
    void Trace(Vector3 origin,Vector3 direction,float& closest)const {
        tree.TraceRay(origin,direction,closest,[&](size_t t,float& nearest){
            const auto& tri=triangles[t];auto e1=tri.b-tri.a,e2=tri.c-tri.a,p=glm::cross(direction,e2);
            float determinant=glm::dot(e1,p);
            if(std::abs(determinant)<=std::numeric_limits<float>::epsilon()*glm::length(e1)*glm::length(p))return;
            auto offset=origin-tri.a;float u=glm::dot(offset,p)/determinant;if(u<-.00001f||u>1.00001f)return;
            auto q=glm::cross(offset,e1);float v=glm::dot(direction,q)/determinant;if(v<-.00001f||u+v>1.00001f)return;
            float distance=glm::dot(e2,q)/determinant;if(distance>0.f&&distance<nearest)nearest=distance;
        });
    }
public:
    void Rebuild(const ShapeMesh& mesh){
        RICE_BENCHMARK_FUNCTION();
        triangles.clear();triangles.reserve(mesh.indices.size()/3);
        std::vector<SpatialBounds> bounds;bounds.reserve(mesh.indices.size()/3);
        for(size_t t=0;t+2<mesh.indices.size();t+=3){
            Triangle tri{mesh.positions[mesh.indices[t]],mesh.positions[mesh.indices[t+1]],mesh.positions[mesh.indices[t+2]]};
            triangles.push_back(tri);bounds.push_back({glm::min(tri.a,glm::min(tri.b,tri.c)),glm::max(tri.a,glm::max(tri.b,tri.c))});
        }
        tree.Build(bounds);
    }
    std::optional<float> Hit(Vector3 origin,Vector3 direction,float limit=1e30f)const {
        float closest=limit;Trace(origin,direction,closest);
        return closest<limit?std::optional<float>(closest):std::nullopt;
    }
    bool Visible(Vector3 camera,Vector3 point,float tolerance=1e-5f)const {
        auto delta=point-camera;float distance=glm::length(delta);
        return distance>tolerance&&!Hit(camera,delta/distance,distance-tolerance);
    }
};
}
