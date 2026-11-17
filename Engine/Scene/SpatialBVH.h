#pragma once
#include "Rice/Core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace Rice {
struct SpatialBounds {
    Vector3 low{0}, high{0};
    bool Overlaps(const SpatialBounds& other) const {
        return glm::all(glm::lessThanEqual(low,other.high)) &&
               glm::all(glm::lessThanEqual(other.low,high));
    }
    static SpatialBounds Transform(const Matrix4& world,const SpatialBounds& local) {
        // An affine transform of an AABB is bounded exactly by these projected extents,
        // including negative scale, rotation and shear.
        Vector3 center=Vector3(world*Vector4((local.low+local.high)*.5f,1));
        Vector3 extent=(local.high-local.low)*.5f;
        Vector3 projected=glm::abs(Vector3(world[0]))*extent.x+
                          glm::abs(Vector3(world[1]))*extent.y+
                          glm::abs(Vector3(world[2]))*extent.z;
        return {center-projected,center+projected};
    }
};

// Shared longest-axis median BVH, extracted from MeshIntersection. Leaves keep
// source indices so clients can retain their own submission/contact ordering.
class SpatialBVH {
    struct Node { SpatialBounds bounds; int left=-1,right=-1; size_t begin=0,end=0; };
    std::vector<SpatialBounds> bounds;
    std::vector<size_t> order;
    std::vector<Node> nodes;
    SpatialBounds RangeBounds(size_t begin,size_t end) const {
        SpatialBounds result{Vector3(std::numeric_limits<float>::max()),Vector3(std::numeric_limits<float>::lowest())};
        for(size_t i=begin;i<end;++i){result.low=glm::min(result.low,bounds[order[i]].low);result.high=glm::max(result.high,bounds[order[i]].high);}
        return result;
    }
    int BuildNode(size_t begin,size_t end) {
        int id=static_cast<int>(nodes.size());nodes.emplace_back();
        nodes[id].bounds=RangeBounds(begin,end);nodes[id].begin=begin;nodes[id].end=end;
        if(end-begin>8){
            auto extent=nodes[id].bounds.high-nodes[id].bounds.low;
            int axis=extent.x>extent.y?0:1;if(extent.z>extent[axis])axis=2;
            size_t mid=(begin+end)/2;
            std::nth_element(order.begin()+begin,order.begin()+mid,order.begin()+end,[&](size_t a,size_t b){
                float ca=bounds[a].low[axis]+bounds[a].high[axis],cb=bounds[b].low[axis]+bounds[b].high[axis];
                return ca==cb?a<b:ca<cb;
            });
            int left=BuildNode(begin,mid),right=BuildNode(mid,end);
            nodes[id].left=left;nodes[id].right=right;
        }
        return id;
    }
    template<class Predicate,class Callback>
    void QueryNode(int id,Predicate& intersects,Callback& visit) const {
        const auto& node=nodes[id];if(!intersects(node.bounds))return;
        if(node.left>=0){QueryNode(node.left,intersects,visit);QueryNode(node.right,intersects,visit);}
        else for(size_t i=node.begin;i<node.end;++i)if(intersects(bounds[order[i]]))visit(order[i]);
    }
    static bool RayEntry(const SpatialBounds& box,Vector3 origin,Vector3 direction,float limit,float& enter) {
        enter=0;float leave=limit;
        for(int axis=0;axis<3;++axis){
            if(std::abs(direction[axis])<1e-10f){
                if(origin[axis]<box.low[axis]-1e-6f||origin[axis]>box.high[axis]+1e-6f)return false;
                continue;
            }
            float first=(box.low[axis]-1e-6f-origin[axis])/direction[axis];
            float second=(box.high[axis]+1e-6f-origin[axis])/direction[axis];
            if(first>second)std::swap(first,second);
            enter=std::max(enter,first);leave=std::min(leave,second);if(enter>leave)return false;
        }
        return true;
    }
    template<class Callback>
    void TraceNode(int id,Vector3 origin,Vector3 direction,float& closest,Callback& visit) const {
        const auto& node=nodes[id];float enter;
        if(!RayEntry(node.bounds,origin,direction,closest,enter))return;
        if(node.left<0){for(size_t i=node.begin;i<node.end;++i)visit(order[i],closest);return;}
        float leftEntry,rightEntry;
        bool left=RayEntry(nodes[node.left].bounds,origin,direction,closest,leftEntry);
        bool right=RayEntry(nodes[node.right].bounds,origin,direction,closest,rightEntry);
        // Test the near child first; the second traversal observes the tightened limit.
        if(left&&right){
            int first=leftEntry<=rightEntry?node.left:node.right,second=leftEntry<=rightEntry?node.right:node.left;
            TraceNode(first,origin,direction,closest,visit);TraceNode(second,origin,direction,closest,visit);
        }else if(left)TraceNode(node.left,origin,direction,closest,visit);
        else if(right)TraceNode(node.right,origin,direction,closest,visit);
    }
public:
    void Build(const std::vector<SpatialBounds>& values) {
        bounds=values;order.resize(bounds.size());std::iota(order.begin(),order.end(),size_t(0));nodes.clear();
        if(!bounds.empty()){nodes.reserve(bounds.size()/2+1);BuildNode(0,bounds.size());}
    }
    // Input ordering must match Build. A changed count starts a new topology.
    void Refit(const std::vector<SpatialBounds>& values) {
        if(values.size()!=bounds.size()){Build(values);return;}
        bounds=values;
        for(size_t i=nodes.size();i>0;--i){
            auto& node=nodes[i-1];
            if(node.left<0)node.bounds=RangeBounds(node.begin,node.end);
            else{
                node.bounds.low=glm::min(nodes[node.left].bounds.low,nodes[node.right].bounds.low);
                node.bounds.high=glm::max(nodes[node.left].bounds.high,nodes[node.right].bounds.high);
            }
        }
    }
    size_t Size() const{return bounds.size();}
    size_t NodeCount() const{return nodes.size();}
    template<class Predicate,class Callback>
    void Query(Predicate intersects,Callback visit) const {if(!nodes.empty())QueryNode(0,intersects,visit);}
    template<class Callback>
    void QueryOverlaps(const SpatialBounds& query,Callback visit) const {
        Query([&](const SpatialBounds& box){return box.Overlaps(query);},visit);
    }
    template<class Callback>
    void QueryFrustum(const Matrix4& viewProjection,Callback visit) const {
        // OpenGL clip inequalities: -w <= x,y,z <= w. Unnormalised planes
        // avoid six square roots and work for perspective/orthographic cameras.
        Matrix4 rows=glm::transpose(viewProjection);
        std::array<Vector4,6> planes{rows[3]+rows[0],rows[3]-rows[0],rows[3]+rows[1],rows[3]-rows[1],rows[3]+rows[2],rows[3]-rows[2]};
        Query([&](const SpatialBounds& box){
            Vector3 center=(box.low+box.high)*.5f,extent=(box.high-box.low)*.5f;
            for(const auto& plane:planes){
                Vector3 normal(plane);float radius=glm::dot(glm::abs(normal),extent);
                float distance=glm::dot(normal,center)+plane.w;
                float tolerance=1e-6f*(1.f+std::abs(plane.w)+glm::dot(glm::abs(normal),glm::abs(center)+extent));
                if(distance+radius < -tolerance)return false;
            }
            return true;
        },visit);
    }
    template<class Callback>
    void TraceRay(Vector3 origin,Vector3 direction,float& closest,Callback visit) const {
        if(!nodes.empty())TraceNode(0,origin,direction,closest,visit);
    }
};
}
