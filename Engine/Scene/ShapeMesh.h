#pragma once
#include "Rice/Benchmark.h"
#include "Scene.h"
#include <array>
#include <iomanip>
#include <istream>
#include <ostream>
#include <map>
#include <limits>

namespace Rice {
enum class BasicShape { Cube, Circle, Rectangle, Sphere, Cylinder, Cone, Custom };

struct MeshFaceGroup {
    std::string name;
    std::vector<uint32_t> triangles;
    bool operator==(const MeshFaceGroup&) const = default;
};

// Positions are shared across faces. UVs and authored normals belong to index-buffer
// corners so a single editable position can have texture seams and hard edges.
struct ShapeMesh {
    std::vector<Vector3> positions;
    std::vector<uint32_t> indices;
    std::vector<Vector2> uv;
    uint64_t revision=1;
    std::vector<MeshFaceGroup> faceGroups;
    std::vector<Vector3> normals; // Empty, or one per corner. A zero entry uses the geometric face normal.

    ShapeMesh()=default;
    ShapeMesh(const ShapeMesh&)=default;
    ShapeMesh(ShapeMesh&&) noexcept=default;
    // Replacing a mesh invalidates caches on its owner even when both meshes have the same revision.
    ShapeMesh& operator=(ShapeMesh replacement) noexcept {
        positions.swap(replacement.positions);
        indices.swap(replacement.indices);
        uv.swap(replacement.uv);
        faceGroups.swap(replacement.faceGroups);
        normals.swap(replacement.normals);
        ++revision;
        return *this;
    }
    bool operator==(const ShapeMesh& other)const {
        return positions==other.positions&&indices==other.indices&&uv==other.uv&&faceGroups==other.faceGroups&&normals==other.normals;
    }

    void Touch(){ RICE_BENCHMARK_FUNCTION(); ++revision; }
    bool Empty()const { return positions.empty(); }
    std::pair<Vector3,Vector3> Bounds()const {
        RICE_BENCHMARK_FUNCTION();
        if(Empty())return {Vector3(-.5f),Vector3(.5f)};
        Vector3 low=positions.front(),high=low;
        for(auto p:positions){low=glm::min(low,p);high=glm::max(high,p);}
        return {low,high};
    }
    std::vector<uint32_t> VertexOrder()const {
        RICE_BENCHMARK_FUNCTION();
        std::vector<uint32_t> order;std::vector<bool> seen(positions.size(),false);
        for(auto index:indices)if(index<seen.size()&&!seen[index]){seen[index]=true;order.push_back(index);}
        return order;
    }
    void Validate()const {
        RICE_BENCHMARK_FUNCTION();
        if(positions.size()>100000||indices.size()>600000||indices.size()%3||uv.size()!=indices.size()||
           (positions.empty()!=indices.empty()))throw std::runtime_error("Invalid custom mesh size");
        for(auto p:positions)for(int a=0;a<3;++a)if(!std::isfinite(p[a]))throw std::runtime_error("Invalid custom vertex");
        for(auto index:indices)if(index>=positions.size())throw std::runtime_error("Invalid custom mesh index");
        for(auto p:uv)if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::runtime_error("Invalid custom UV");
        ValidateNormals();
        if(!faceGroups.empty()){
            const size_t triangleCount=indices.size()/3;
            if(faceGroups.size()>triangleCount)throw std::runtime_error("Too many face groups");
            std::vector<bool> assigned(triangleCount);
            for(const auto& group:faceGroups){
                if(group.name.size()>65536||group.triangles.empty())throw std::runtime_error("Invalid face group");
                for(auto triangle:group.triangles){
                    if(triangle>=triangleCount||assigned[triangle])throw std::runtime_error("Invalid face group triangle");
                    assigned[triangle]=true;
                }
            }
        }
    }
    void Write(std::ostream& out)const {
        RICE_BENCHMARK_FUNCTION();
        if(!normals.empty()){ValidateNormals();out<<"CornerNormals ";}
        if(!faceGroups.empty())out<<"GroupedMesh ";
        out<<positions.size()<<' '<<indices.size()<<' ';
        for(auto p:positions)out<<p.x<<' '<<p.y<<' '<<p.z<<' ';
        for(auto i:indices)out<<i<<' ';
        for(auto p:uv)out<<p.x<<' '<<p.y<<' ';
        for(auto n:normals)out<<n.x<<' '<<n.y<<' '<<n.z<<' ';
        if(!faceGroups.empty()){
            out<<faceGroups.size()<<' ';
            for(const auto& group:faceGroups){
                out<<std::quoted(group.name)<<' '<<group.triangles.size()<<' ';
                for(auto triangle:group.triangles)out<<triangle<<' ';
            }
        }
    }
    void Read(std::istream& in){
        RICE_BENCHMARK_FUNCTION();
        ShapeMesh next;size_t vertices=0,corners=0;
        in>>std::ws;
        const bool hasNormals=in.peek()=='C';
        if(hasNormals){
            std::string format;in>>format>>std::ws;
            if(format!="CornerNormals")throw std::runtime_error("Invalid custom mesh format");
        }
        const bool grouped=in.peek()=='G';
        if(grouped){
            std::string format;in>>format;
            if(format!="GroupedMesh")throw std::runtime_error("Invalid custom mesh format");
        }
        in>>vertices>>corners;
        if(!in||vertices>100000||corners>600000||corners%3)throw std::runtime_error("Invalid custom mesh size");
        next.positions.resize(vertices);next.indices.resize(corners);next.uv.resize(corners);
        for(auto& p:next.positions)in>>p.x>>p.y>>p.z;
        for(auto& i:next.indices)in>>i;
        for(auto& p:next.uv)in>>p.x>>p.y;
        if(hasNormals){
            next.normals.resize(corners);
            for(auto& n:next.normals)in>>n.x>>n.y>>n.z;
        }
        if(grouped){
            size_t groupCount=0,remaining=corners/3;
            in>>groupCount;
            if(!in||!groupCount||groupCount>remaining)throw std::runtime_error("Invalid face group count");
            next.faceGroups.resize(groupCount);
            for(auto& group:next.faceGroups){
                size_t count=0;in>>std::quoted(group.name)>>count;
                if(!in||!count||count>remaining)throw std::runtime_error("Invalid face group size");
                remaining-=count;
                group.triangles.resize(count);
                for(auto& triangle:group.triangles)in>>triangle;
            }
        }
        if(!in)throw std::runtime_error("Truncated custom mesh");
        next.Validate();*this=std::move(next);
    }
    static ShapeMesh FromPrimitive(BasicShape shape){
        RICE_BENCHMARK_FUNCTION();
        ShapeMesh mesh;
        std::map<std::array<int,3>,uint32_t> welded;
        auto corner=[&](Vector3 p,Vector2 tex){
            std::array<int,3> key{int(std::round(p.x*100000)),int(std::round(p.y*100000)),int(std::round(p.z*100000))};
            auto [it,inserted]=welded.try_emplace(key,static_cast<uint32_t>(mesh.positions.size()));
            if(inserted)mesh.positions.push_back(p);
            mesh.indices.push_back(it->second);mesh.uv.push_back(tex);
        };
        auto triangle=[&](Vector3 a,Vector3 b,Vector3 c){
            if(glm::length(glm::cross(b-a,c-a))<1e-8f)return;
            corner(a,{a.x+.5f,a.y+.5f});corner(b,{b.x+.5f,b.y+.5f});corner(c,{c.x+.5f,c.y+.5f});
        };
        if(shape==BasicShape::Cube||shape==BasicShape::Rectangle||shape==BasicShape::Custom){
            const Vector3 normals[]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
            bool flat=shape==BasicShape::Rectangle;
            for(int face=0;face<(flat?1:6);++face){
                auto n=normals[face];auto u=std::abs(n.y)>.5f?Vector3(1,0,0):glm::normalize(glm::cross(Vector3(0,1,0),n));auto v=glm::cross(n,u);
                const Vector2 tex[]={{0,0},{1,0},{1,1},{0,1}};
                for(int k:{0,1,2,2,3,0})corner(u*(tex[k].x-.5f)+v*(tex[k].y-.5f)+(flat?Vector3(0):n*.5f),tex[k]);
            }
        }else{
            constexpr int segments=48,rings=24;constexpr float pi=3.14159265359f;
            for(int i=0;i<segments;++i){
                float a=2*pi*i/segments,b=2*pi*(i+1)/segments;
                Vector3 ra(std::cos(a),0,std::sin(a)),rb(std::cos(b),0,std::sin(b));
                if(shape==BasicShape::Circle)triangle({0,0,0},{.5f*std::cos(a),.5f*std::sin(a),0},{.5f*std::cos(b),.5f*std::sin(b),0});
                else if(shape==BasicShape::Sphere){
                    auto point=[](float longitude,float latitude){return .5f*Vector3(std::sin(latitude)*std::cos(longitude),std::cos(latitude),std::sin(latitude)*std::sin(longitude));};
                    for(int j=0;j<rings;++j){auto p=point(a,pi*j/rings),q=point(b,pi*j/rings),r=point(a,pi*(j+1)/rings),t=point(b,pi*(j+1)/rings);triangle(p,q,r);triangle(q,t,r);}
                }else{
                    auto ba=ra*.5f+Vector3(0,-.5f,0),bb=rb*.5f+Vector3(0,-.5f,0);
                    triangle({0,-.5f,0},ba,bb);
                    if(shape==BasicShape::Cone)triangle(ba,{0,.5f,0},bb);
                    else{auto ta=ba+Vector3(0,1,0),tb=bb+Vector3(0,1,0);triangle(ba,ta,bb);triangle(bb,ta,tb);triangle({0,.5f,0},tb,ta);}
                }
            }
        }
        return mesh;
    }
    // Expand the corners for drawing. Missing normals follow the triangle winding.
    std::vector<float> RenderVertices()const {
        RICE_BENCHMARK_FUNCTION();
        std::vector<float> result;result.reserve(indices.size()*8);
        const bool hasNormals=normals.size()==indices.size();
        for(size_t t=0;t+2<indices.size();t+=3){
            const glm::dvec3 first(positions[indices[t]]), second(positions[indices[t+1]]), third(positions[indices[t+2]]);
            const auto cross=glm::cross(second-first,third-first);
            // Imported dimensions are in meters. Tiny, valid faces still have a surface normal.
            const auto faceNormal=glm::dot(cross,cross)>0.0?Vector3(glm::normalize(cross)):Vector3(0,0,1);
            for(size_t c=t;c<t+3;++c){
                auto n=faceNormal;
                if(hasNormals){
                    const glm::dvec3 authored(normals[c]);
                    const double lengthSquared=glm::dot(authored,authored);
                    if(lengthSquared>0&&std::isfinite(lengthSquared))n=Vector3(authored/std::sqrt(lengthSquared));
                }
                auto p=positions[indices[c]];auto tex=uv[c];
                result.insert(result.end(),{p.x,p.y,p.z,n.x,n.y,n.z,tex.x,tex.y});
            }
        }
        return result;
    }
private:
    void ValidateNormals()const {
        if(!normals.empty()&&normals.size()!=indices.size())throw std::runtime_error("Invalid custom normal count");
        for(auto n:normals)if(!std::isfinite(n.x)||!std::isfinite(n.y)||!std::isfinite(n.z))
            throw std::runtime_error("Invalid custom normal");
    }
};
}
