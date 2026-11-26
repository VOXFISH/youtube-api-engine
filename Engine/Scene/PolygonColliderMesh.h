#pragma once
#include "MeshRenderer.h"
#include <numeric>

namespace Rice {

// A collider references its renderer's triangles and local BVH, including empty space
// between disconnected parts. Moving an object does not rebuild the triangle tree.
class PolygonColliderMesh {
    struct Primitive {
        ShapeMesh shape;
        MeshIntersection intersection;
        explicit Primitive(BasicShape type):shape(ShapeMesh::FromPrimitive(type)){intersection.Rebuild(shape);}
    };
    static const Primitive& BuiltIn(int shape){
        static const Primitive primitives[]{Primitive(BasicShape::Cube),Primitive(BasicShape::Circle),
            Primitive(BasicShape::Rectangle),Primitive(BasicShape::Sphere),Primitive(BasicShape::Cylinder),Primitive(BasicShape::Cone)};
        return primitives[std::clamp(shape,0,5)];
    }
    const ShapeMesh* source=nullptr;
    const MeshIntersection* intersection=nullptr;
    uint64_t revision=0;
    Matrix4 world{1};
    SpatialBounds linearBounds{},bounds{};
    mutable bool topologyReady=false;
    mutable std::vector<uint8_t> solidFaces;
    mutable std::vector<uint32_t> islandSamples;
    mutable bool hasSolid=false;

    // Weld position seams for topology only. Collision triangles keep the exact
    // coordinates used for rendering. Only closed, manifold islands enclose space.
    void PrepareTopology() const {
        if(topologyReady)return;
        const size_t count=TriangleCount();
        std::vector<size_t> parents(count);
        std::iota(parents.begin(),parents.end(),size_t{0});
        auto root=[&](size_t face){
            while(parents[face]!=face){parents[face]=parents[parents[face]];face=parents[face];}
            return face;
        };
        std::map<std::array<float,3>,uint32_t> positions;
        std::vector<uint32_t> welded;
        welded.reserve(source->positions.size());
        for(auto point:source->positions){
            auto entry=positions.try_emplace({point.x,point.y,point.z},static_cast<uint32_t>(positions.size()));
            welded.push_back(entry.first->second);
        }
        struct Edge {size_t face;unsigned uses;};
        std::map<std::pair<uint32_t,uint32_t>,Edge> edges;
        std::vector<bool> valid(count,false);
        for(size_t face=0;face<count;++face){
            const auto* corners=&source->indices[face*3];
            const auto a=glm::dvec3(source->positions[corners[0]]);
            const auto normal=glm::cross(glm::dvec3(source->positions[corners[1]])-a,glm::dvec3(source->positions[corners[2]])-a);
            if(glm::dot(normal,normal)==0)continue;
            valid[face]=true;
            for(int i=0;i<3;++i){
                auto key=std::minmax(welded[corners[i]],welded[corners[(i+1)%3]]);
                auto [it,inserted]=edges.try_emplace(key,Edge{face,0});
                ++it->second.uses;
                if(!inserted)parents[root(face)]=root(it->second.face);
            }
        }
        std::vector<bool> closed(count,true),sampled(count);
        for(const auto& [edge,use]:edges)if(use.uses!=2)closed[root(use.face)]=false;
        solidFaces.assign(count,0);
        islandSamples.clear();hasSolid=false;
        for(size_t face=0;face<count;++face)if(valid[face]){
            size_t island=root(face);
            if(!sampled[island]){islandSamples.push_back(source->indices[face*3]);sampled[island]=true;}
            solidFaces[face]=closed[island];
            hasSolid|=closed[island];
        }
        topologyReady=true;
    }
public:
    bool Prepare(MeshRenderer* mesh,const Matrix4& transform){
        if(!mesh || !mesh->HasGeometry()){
            source=nullptr;intersection=nullptr;bounds={};topologyReady=false;
            return false;
        }
        const ShapeMesh* next;
        if(mesh->shape==int(BasicShape::Custom)){
            next=&mesh->custom;intersection=&mesh->Intersection();
        }else{
            const auto& primitive=BuiltIn(mesh->shape);
            next=&primitive.shape;intersection=&primitive.intersection;
        }
        bool changed=source!=next||revision!=next->revision;
        if(!changed&&world==transform)return true;
        const glm::mat3 linear(transform);
        if(changed||linear!=glm::mat3(world)){
            // Use the referenced vertices so rotated pieces rest on their actual lowest point.
            linearBounds={Vector3(std::numeric_limits<float>::max()),Vector3(std::numeric_limits<float>::lowest())};
            for(auto index:next->indices){
                auto p=linear*next->positions[index];
                linearBounds.low=glm::min(linearBounds.low,p);
                linearBounds.high=glm::max(linearBounds.high,p);
            }
        }
        if(changed)topologyReady=false;
        source=next;revision=next->revision;world=transform;
        bounds={linearBounds.low+Vector3(world[3]),linearBounds.high+Vector3(world[3])};
        return true;
    }
    SpatialBounds Bounds() const{return bounds;}
    size_t TriangleCount() const{return source?source->indices.size()/3:0;}
    std::array<Vector3,3> Triangle(size_t face) const{
        std::array<Vector3,3> result;
        for(int i=0;i<3;++i)result[i]=Vector3(world*Vector4(source->positions[source->indices[3*face+i]],1));
        return result;
    }
    std::pair<float,float> Project(Vector3 axis,Vector3 origin) const {
        auto localAxis=glm::transpose(glm::mat3(world))*axis;
        const auto& tree=intersection->Hierarchy();
        auto extreme=[&](Vector3 direction){
            float best=std::numeric_limits<float>::lowest();
            auto upper=[&](int id){
                const auto& box=tree.GetNode(id).bounds;
                return glm::dot(direction,glm::mix(box.low,box.high,glm::greaterThanEqual(direction,Vector3(0))));
            };
            auto visit=[&](auto&& self,int id,float bound)->void{
                if(bound<=best)return;
                const auto& node=tree.GetNode(id);
                if(node.left<0){
                    for(size_t i=node.begin;i<node.end;++i){
                        const auto* corners=&source->indices[3*tree.PrimitiveIndex(i)];
                        for(int j=0;j<3;++j)best=std::max(best,glm::dot(direction,source->positions[corners[j]]));
                    }
                    return;
                }
                float left=upper(node.left),right=upper(node.right);
                if(left>right){self(self,node.left,left);self(self,node.right,right);}
                else{self(self,node.right,right);self(self,node.left,left);}
            };
            visit(visit,0,upper(0));return best;
        };
        float offset=glm::dot(axis,Vector3(world[3])-origin);
        return {offset-extreme(-localAxis),offset+extreme(localAxis)};
    }
    template<class F> void QueryWorldBounds(const SpatialBounds& query,F visit) const {
        if(!intersection)return;
        intersection->Hierarchy().Query([&](const SpatialBounds& local){
            auto box=SpatialBounds::Transform(world,local);
            auto pad=(glm::max(glm::abs(box.low),glm::abs(box.high))+Vector3(1))*(4*std::numeric_limits<float>::epsilon());
            return SpatialBounds{box.low-pad,box.high+pad}.Overlaps(query);
        },visit);
    }
    template<class F> void ForEachIslandPoint(F visit) const {
        if(!source)return;
        PrepareTopology();
        for(auto index:islandSamples)visit(Vector3(world*Vector4(source->positions[index],1)));
    }
    bool Contains(Vector3 worldPoint) const{
        if(!source || !bounds.Overlaps({worldPoint,worldPoint}))return false;
        PrepareTopology();
        if(!hasSolid)return false;
        glm::dmat4 matrix(world);
        if(glm::determinant(matrix)==0)return false;
        glm::dvec3 origin(glm::inverse(matrix)*glm::dvec4(worldPoint,1));
        // Retry a ray that hits a shared edge. This avoids counting the same surface twice.
        static constexpr std::array<Vector3,3> directions{{{1,.46373f,.31719f},{-.32871f,1,.58491f},{.21853f,-.73917f,1}}};
        for(auto ray:directions){
            glm::dvec3 direction(ray);
            size_t crossings=0;bool onEdge=false;
            float limit=std::numeric_limits<float>::max();
            intersection->Hierarchy().TraceRay(Vector3(origin),ray,limit,[&](size_t face,float&){
                if(!solidFaces[face])return;
                const auto* corners=&source->indices[3*face];
                glm::dvec3 a(source->positions[corners[0]]);
                auto e1=glm::dvec3(source->positions[corners[1]])-a;
                auto e2=glm::dvec3(source->positions[corners[2]])-a;
                auto p=glm::cross(direction,e2);double det=glm::dot(e1,p);
                if(std::abs(det)<=std::numeric_limits<double>::epsilon()*glm::length(e1)*glm::length(p))return;
                auto offset=origin-a,q=glm::cross(offset,e1);
                double u=glm::dot(offset,p)/det,v=glm::dot(direction,q)/det;
                if(u<0||v<0||u+v>1||glm::dot(e2,q)/det<=0)return;
                ++crossings;
                if(std::min({u,v,1-u-v})<1e-9)onEdge=true;
            });
            if(!onEdge)return crossings%2!=0;
        }
        return false;
    }
};
}
