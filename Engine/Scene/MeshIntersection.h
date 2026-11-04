#pragma once
#include "ShapeMesh.h"
#include <optional>

namespace Rice {
// Cached triangle BVH: visibility checks stay responsive on sphere meshes.
class MeshIntersection {
    struct Triangle {Vector3 a,b,c;};
    struct Node {Vector3 low{0},high{0};int left=-1,right=-1;size_t begin=0,end=0;};
    std::vector<Triangle> triangles;
    std::vector<Node> nodes;
    int Build(size_t begin,size_t end){
        int id=static_cast<int>(nodes.size());nodes.emplace_back();
        Vector3 low(1e30f),high(-1e30f);
        for(size_t i=begin;i<end;++i)for(auto p:{triangles[i].a,triangles[i].b,triangles[i].c}){low=glm::min(low,p);high=glm::max(high,p);}
        nodes[id].low=low;nodes[id].high=high;nodes[id].begin=begin;nodes[id].end=end;
        if(end-begin>8){
            auto extent=high-low;int axis=extent.x>extent.y?0:1;if(extent.z>extent[axis])axis=2;
            size_t mid=(begin+end)/2;
            std::nth_element(triangles.begin()+begin,triangles.begin()+mid,triangles.begin()+end,[axis](const auto& a,const auto& b){return a.a[axis]+a.b[axis]+a.c[axis]<b.a[axis]+b.b[axis]+b.c[axis];});
            int left=Build(begin,mid),right=Build(mid,end);nodes[id].left=left;nodes[id].right=right;
        }
        return id;
    }
    void Trace(int id,Vector3 origin,Vector3 direction,float& closest)const {
        const auto& node=nodes[id];float enter=0,leave=closest;
        for(int a=0;a<3;++a){
            if(std::abs(direction[a])<1e-10f){if(origin[a]<node.low[a]-1e-6f||origin[a]>node.high[a]+1e-6f)return;continue;}
            float p=(node.low[a]-1e-6f-origin[a])/direction[a],q=(node.high[a]+1e-6f-origin[a])/direction[a];
            if(p>q)std::swap(p,q);enter=std::max(enter,p);leave=std::min(leave,q);if(enter>leave)return;
        }
        if(node.left>=0){Trace(node.left,origin,direction,closest);Trace(node.right,origin,direction,closest);return;}
        for(size_t t=node.begin;t<node.end;++t){
            const auto& tri=triangles[t];auto e1=tri.b-tri.a,e2=tri.c-tri.a,p=glm::cross(direction,e2);
            float determinant=glm::dot(e1,p);if(std::abs(determinant)<1e-10f)continue;
            auto offset=origin-tri.a;float u=glm::dot(offset,p)/determinant;if(u<-.00001f||u>1.00001f)continue;
            auto q=glm::cross(offset,e1);float v=glm::dot(direction,q)/determinant;if(v<-.00001f||u+v>1.00001f)continue;
            float distance=glm::dot(e2,q)/determinant;if(distance>1e-6f&&distance<closest)closest=distance;
        }
    }
public:
    void Rebuild(const ShapeMesh& mesh){
        RICE_BENCHMARK_FUNCTION();
        triangles.clear();nodes.clear();triangles.reserve(mesh.indices.size()/3);
        for(size_t t=0;t+2<mesh.indices.size();t+=3)triangles.push_back({mesh.positions[mesh.indices[t]],mesh.positions[mesh.indices[t+1]],mesh.positions[mesh.indices[t+2]]});
        if(!triangles.empty())Build(0,triangles.size());
    }
    std::optional<float> Hit(Vector3 origin,Vector3 direction,float limit=1e30f)const {
        float closest=limit;if(!nodes.empty())Trace(0,origin,direction,closest);
        return closest<limit?std::optional<float>(closest):std::nullopt;
    }
    bool Visible(Vector3 camera,Vector3 point,float tolerance=1e-5f)const {
        auto delta=point-camera;float distance=glm::length(delta);
        return distance>tolerance&&!Hit(camera,delta/distance,distance-tolerance);
    }
};
}
