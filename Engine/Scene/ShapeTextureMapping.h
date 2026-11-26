#pragma once
#include "ShapeFaceGroups.h"

namespace Rice {
inline constexpr std::array<Vector2,3> RepeatedTriangleUVs{{{0,0},{1,0},{0,1}}};

inline bool HasRepeatedTriangleUVs(const ShapeMesh& mesh,const ShapeFaceGroups::FaceGroup& group) {
    for(auto triangle:group.triangles)for(size_t c=0;c<3;++c)
        if(mesh.uv[3*triangle+c]!=RepeatedTriangleUVs[c])return false;
    return !group.triangles.empty();
}

inline bool ApplyGroupTextureUVs(ShapeMesh& mesh,const ShapeFaceGroups::FaceGroup& group,bool linked) {
    if(group.triangles.empty())return false;
    bool changed=false;
    auto assign=[&](size_t corner,Vector2 uv){
        if(mesh.uv[corner]!=uv){mesh.uv[corner]=uv;changed=true;}
    };
    if(!linked){
        for(auto t:group.triangles)for(size_t c=0;c<3;++c)assign(3*t+c,RepeatedTriangleUVs[c]);
    }else{
        auto position=[&](uint32_t index){return glm::dvec3(mesh.positions[index]);};
        auto faceNormal=[&](uint32_t t){
            const auto* indices=mesh.indices.data()+3*t;
            const auto a=position(indices[0]);
            return glm::cross(position(indices[1])-a,position(indices[2])-a);
        };
        auto orient=[](glm::dvec3 vector){
            int axis=0;
            for(int d=1;d<3;++d)if(std::abs(vector[d])>std::abs(vector[axis]))axis=d;
            return vector[axis]<0?-vector:vector;
        };
        // Average area normals in a common hemisphere. Reversed winding does
        // not create seams or cancel the projection plane of a triangulated face.
        glm::dvec3 largest(0),normal(0);
        for(auto t:group.triangles){
            auto n=faceNormal(t);
            if(glm::dot(n,n)>glm::dot(largest,largest))largest=orient(n);
        }
        for(auto t:group.triangles){
            auto n=orient(faceNormal(t));
            normal+=glm::dot(n,largest)<0?-n:n;
        }
        const auto origin=position(group.vertices.front());
        glm::dvec3 u(0),v(0);
        if(glm::dot(normal,normal)>0){
            normal=glm::normalize(orient(normal));
            const glm::dvec3 up=std::abs(normal.y)>.95?glm::dvec3(0,0,-1):glm::dvec3(0,1,0);
            u=glm::normalize(glm::cross(up,normal));v=glm::cross(normal,u);
        }else{
            // A line spans U only, and a collapsed point samples the center.
            for(auto vertex:group.vertices){
                const auto delta=position(vertex)-origin;
                if(glm::dot(delta,delta)>glm::dot(u,u))u=delta;
            }
            if(glm::dot(u,u)>0)u=glm::normalize(orient(u));
        }
        auto project=[&](uint32_t vertex){
            const auto p=position(vertex)-origin;
            return glm::dvec2(glm::dot(p,u),glm::dot(p,v));
        };
        glm::dvec2 low(0),high(0);
        for(auto vertex:group.vertices){
            const auto p=project(vertex);
            low=glm::min(low,p);high=glm::max(high,p);
        }
        const auto span=high-low;
        for(auto t:group.triangles)for(size_t c=0;c<3;++c){
            size_t corner=3*t+c;
            const auto p=project(mesh.indices[corner])-low;
            assign(corner,{float(span.x>0?p.x/span.x:.5),float(span.y>0?p.y/span.y:.5)});
        }
    }
    if(changed)mesh.Touch();
    return changed;
}
}
