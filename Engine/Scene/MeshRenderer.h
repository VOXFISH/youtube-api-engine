#pragma once
#include "MeshIntersection.h"
#include "TriangleMaterials.h"
#include "Renderer/VertexArray.h"

namespace Rice {
enum class ReflectionMode { Off, Direct, Precise };
struct MeshRenderer : Component {
    bool castsShadow=true;
    int reflectionMode = int(ReflectionMode::Precise);
    Color color{0.7f,0.7f,0.8f,1};
    int shape = int(BasicShape::Cube);
    ShapeMesh custom;
    bool HasGeometry()const{return shape!=int(BasicShape::Custom)||!custom.indices.empty();}
    std::string shapeAsset;
    TriangleMaterials triangleMaterials;
    mutable uint64_t boundsRevision=0;
    mutable const Vector3* boundsPositions=nullptr;
    mutable size_t boundsPositionCount=0;
    mutable std::pair<Vector3,Vector3> cachedLocalBounds;
    std::optional<std::pair<Vector3,Vector3>> GeometryBounds()const override{
        if(shape==int(BasicShape::Custom)){
            if(!GameObject::WorldCacheEnabled())return custom.Bounds();
            if(boundsRevision!=custom.revision||boundsPositions!=custom.positions.data()||boundsPositionCount!=custom.positions.size()){
                cachedLocalBounds=custom.Bounds();boundsRevision=custom.revision;
                boundsPositions=custom.positions.data();boundsPositionCount=custom.positions.size();
            }
            return cachedLocalBounds;
        }
        if(shape==int(BasicShape::Circle)||shape==int(BasicShape::Rectangle))return std::pair{Vector3(-.5f,-.5f,0),Vector3(.5f,.5f,0)};
        return std::pair{Vector3(-.5f),Vector3(.5f)};
    }
    Ref<VertexArray> customArray;
    uint64_t uploadedRevision=0;
    MeshIntersection intersection;
    uint64_t intersectionRevision=0;
    void ValidateSettings()const{
        triangleMaterials.Validate(custom.indices.size()/3);
        if(reflectionMode < int(ReflectionMode::Off) || reflectionMode > int(ReflectionMode::Precise))
            throw std::runtime_error("Invalid mesh reflection mode");
    }
    const MeshIntersection& Intersection(){
        RICE_BENCHMARK_FUNCTION();
        if(intersectionRevision!=custom.revision||intersection.Hierarchy().Size()!=custom.indices.size()/3){
            intersection.Rebuild(custom);intersectionRevision=custom.revision;
        }
        return intersection;
    }
    void SetShape(int next){
        RICE_BENCHMARK_FUNCTION();
        if(next==int(BasicShape::Custom)&&custom.Empty())custom=ShapeMesh::FromPrimitive(BasicShape(std::clamp(shape,0,5)));
        shape=next;
    }
};
}
