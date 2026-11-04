#pragma once
#include "GizmoMath.h"
#include "Scene/MeshIntersection.h"
#include <set>

namespace Rice {
struct ShapeSelection {
    std::vector<uint32_t> order;
    std::set<uint32_t> selected;
    int active=-1,anchor=-1,cursor=-1; // active is the last clicked/edited pivot
    int triangle=-1;
    void Reset(const ShapeMesh& mesh){ RICE_BENCHMARK_FUNCTION(); order=mesh.VertexOrder();selected.clear();active=anchor=cursor=triangle=-1; }
    void SelectTriangle(const ShapeMesh& mesh,int index){
        if(index<0||size_t(index)>=mesh.indices.size()/3)return;
        triangle=index;selected.clear();for(int i=0;i<3;++i)selected.insert(mesh.indices[index*3+i]);active=anchor=cursor=-1;
    }
    void Click(int row,bool shift,bool range=false){
        RICE_BENCHMARK_FUNCTION();
        if(row<0||row>=static_cast<int>(order.size()))return;
        if(shift||!selected.contains(order[row]))triangle=-1;
        if(shift&&range&&anchor>=0){selected.clear();for(int i=std::min(anchor,row);i<=std::max(anchor,row);++i)selected.insert(order[i]);}
        else{if(!shift&&!selected.contains(order[row]))selected.clear();selected.insert(order[row]);anchor=row;}
        active=cursor=row;
    }
    void Step(int direction,bool shift){
        RICE_BENCHMARK_FUNCTION();
        if(order.empty())return;
        triangle=-1;
        int row=std::clamp(cursor<0?0:cursor+direction,0,static_cast<int>(order.size())-1);
        if(shift&&anchor>=0){
            cursor=row;selected.clear();
            for(int i=std::min(anchor,row);i<=std::max(anchor,row);++i)selected.insert(order[i]);
            // Keyboard range navigation never silently changes the clicked pivot.
            if(active>=0)selected.insert(order[active]);
        }else{selected.clear();Click(row,false);}
    }
    int ActiveVertex()const {return active>=0&&active<static_cast<int>(order.size())?static_cast<int>(order[active]):-1;}
    void Box(const std::vector<std::optional<Vector3>>& projected,Vector2 start,Vector2 end,const std::set<uint32_t>& base={}){
        RICE_BENCHMARK_FUNCTION();
        triangle=-1;selected=base;auto low=glm::min(start,end),high=glm::max(start,end);
        for(size_t row=0;row<order.size();++row)if(projected[row]){
            Vector2 p(*projected[row]);if(glm::all(glm::greaterThanEqual(p,low))&&glm::all(glm::lessThanEqual(p,high)))selected.insert(order[row]);
        }
        if(ActiveVertex()<0||!selected.contains(static_cast<uint32_t>(ActiveVertex())))active=-1;
    }
};

namespace ShapeEditorMath {
inline constexpr float PointRadius=2.f,HoverRadius=5.f,HitRadius=10.f,CubePixels=20.f;
struct TriangleMarker {std::array<Vector2,3> points;int triangle=-1;float depth=0;};
inline TriangleMarker ScaleMarker(TriangleMarker marker,float scale){
    auto center=(marker.points[0]+marker.points[1]+marker.points[2])/3.f;
    for(auto& p:marker.points)p=center+(p-center)*scale;return marker;
}
inline bool HitTriangle(const TriangleMarker& marker,Vector2 mouse){
    bool positive=false,negative=false;
    for(int i=0;i<3;++i){auto edge=marker.points[(i+1)%3]-marker.points[i],offset=mouse-marker.points[i];auto cross=edge.x*offset.y-edge.y*offset.x;positive|=cross>1e-4f;negative|=cross< -1e-4f;}
    return !(positive&&negative);
}
inline std::vector<TriangleMarker> TriangleMarkers(const ShapeMesh& mesh,const MeshIntersection& spatial,Vector3 camera,Vector2 origin,Vector2 size,const Matrix4& vp,Vector3 offset=Vector3(0)){
    std::vector<TriangleMarker> markers;auto [low,high]=mesh.Bounds();float epsilon=std::max(1e-5f,glm::length(high-low)*1e-5f);
    for(size_t t=0;t<mesh.indices.size()/3;++t){
        TriangleMarker marker;marker.triangle=static_cast<int>(t);Vector3 center(0);bool visible=true;
        for(int c=0;c<3;++c){auto vertex=mesh.positions[mesh.indices[t*3+c]];center+=vertex/3.f;auto point=GizmoMath::Project(vertex-offset,origin,size,vp);if(!point){visible=false;break;}marker.points[c]=Vector2(*point);marker.depth+=point->z/3.f;}
        if(!visible)continue;marker=ScaleMarker(marker,.2f);
        Vector2 a=marker.points[0],b=a;for(auto p:marker.points){a=glm::min(a,p);b=glm::max(b,p);}
        auto e1=marker.points[1]-marker.points[0],e2=marker.points[2]-marker.points[0];
        if(std::max(b.x-a.x,b.y-a.y)<=5.f||std::abs(e1.x*e2.y-e1.y*e2.x)<.01f||b.x<origin.x||b.y<origin.y||a.x>origin.x+size.x||a.y>origin.y+size.y)continue;
        if(!spatial.Visible(camera+offset,center,epsilon))continue;
        // Keep markers from showing through another triangle along their edges.
        for(int c=0;c<3;++c)if(!spatial.Visible(camera+offset,center+(mesh.positions[mesh.indices[t*3+c]]-center)*.2f,epsilon)){visible=false;break;}
        if(visible)markers.push_back(marker);
    }
    std::sort(markers.begin(),markers.end(),[](const auto& a,const auto& b){return a.depth>b.depth;});return markers;
}
struct CubeFace {std::array<Vector2,4> points;int axis=0;float sign=1,depth=0;};
struct Cube {std::vector<CubeFace> faces;float side=0;};
inline Cube MakeCube(Vector3 center,Vector3 camera,Vector2 origin,Vector2 size,const Matrix4& vp){
    RICE_BENCHMARK_FUNCTION();
    Cube cube;auto projected=GizmoMath::Project(center,origin,size,vp);if(!projected)return cube;
    // Convert pixels to units at the pivot's camera depth, then normalize the
    // actual projected extent, including perspective and all three axes.
    float depth=(vp*Vector4(center,1)).w;
    float side=CubePixels*2.f*depth*std::tan(glm::radians(45.f)*.5f)/std::max(size.y,1.f);
    std::array<Vector3,8> screen;
    for(int iteration=0;iteration<6;++iteration){
        Vector2 low(1e30f),high(-1e30f);
        for(int k=0;k<8;++k){
            auto p=vp*Vector4(center+Vector3(k&1?.5f:-.5f,k&2?.5f:-.5f,k&4?.5f:-.5f)*side,1);
            if(p.w<=.00001f)return cube;p/=p.w;
            // This overlay is deliberately not clipped against the mesh camera's
            // depth planes; its center alone determines whether it is on screen.
            screen[k]={origin.x+(p.x+1)*.5f*size.x,origin.y+(1-p.y)*.5f*size.y,p.z};
            low=glm::min(low,Vector2(screen[k]));high=glm::max(high,Vector2(screen[k]));
        }
        float extent=std::max(high.x-low.x,high.y-low.y);
        if(iteration==5||extent<1e-6f)break;side*=CubePixels/extent;
    }
    cube.side=side;
    for(int axis=0;axis<3;++axis)for(int sign:{-1,1}){
        if((camera[axis]-center[axis])*sign<=side*.5f)continue;
        int u=(axis+1)%3,v=(axis+2)%3;
        CubeFace face;face.axis=axis;face.sign=float(sign);
        for(int k=0;k<4;++k){int bits=sign>0?1<<axis:0;if(k==1||k==2)bits|=1<<u;if(k>=2)bits|=1<<v;face.points[k]=Vector2(screen[bits]);face.depth+=screen[bits].z*.25f;}
        cube.faces.push_back(face);
    }
    std::sort(cube.faces.begin(),cube.faces.end(),[](const auto& a,const auto& b){return a.depth>b.depth;});return cube;
}
inline int HitFace(const Cube& cube,Vector2 mouse){
    RICE_BENCHMARK_FUNCTION();
    for(int f=static_cast<int>(cube.faces.size())-1;f>=0;--f){
        const auto& face=cube.faces[f];bool positive=false,negative=false;
        for(int k=0;k<4;++k){auto edge=face.points[(k+1)%4]-face.points[k],offset=mouse-face.points[k];float cross=edge.x*offset.y-edge.y*offset.x;positive|=cross>.001f;negative|=cross<-.001f;}
        if(!(positive&&negative))return f;
    }
    return -1;
}
struct TranslationAxis {Vector2 start{0},end{0};float unitsPerPixel=0;int axis=0;};
inline std::vector<TranslationAxis> TranslationAxes(Vector3 position,Vector2 origin,Vector2 size,const Matrix4& vp){
    std::vector<TranslationAxis> axes;auto center=GizmoMath::Project(position,origin,size,vp);if(!center)return axes;
    float unit=2.f*(vp*Vector4(position,1)).w*std::tan(glm::radians(45.f)*.5f)/std::max(1.f,size.y);
    for(int i=0;i<3;++i){Vector3 direction(0);direction[i]=1;auto end=GizmoMath::Project(position+direction*unit*70.f,origin,size,vp);if(!end)continue;
        auto delta=Vector2(*end-*center);float length=glm::length(delta);if(length<8.f)continue;
        auto normal=delta/length;axes.push_back({Vector2(*center)+normal*14.f,Vector2(*center)+normal*70.f,unit*70.f/length,i});
    }
    return axes;
}
inline float SegmentDistance(Vector2 point,Vector2 a,Vector2 b){auto edge=b-a;float length=glm::dot(edge,edge);return glm::length(point-(a+edge*(length>1e-6f?std::clamp(glm::dot(point-a,edge)/length,0.f,1.f):0.f)));}
inline int HitTranslationAxis(const std::vector<TranslationAxis>& axes,Vector2 mouse){
    int hit=-1;float distance=8.f;for(const auto& axis:axes){float d=SegmentDistance(mouse,axis.start,axis.end);if(d<distance){distance=d;hit=axis.axis;}}return hit;
}
inline bool HitCameraIcon(const Matrix4& frame,Vector2 mouse,Vector2 origin,Vector2 size,const Matrix4& vp){
    Vector2 low(1e30f),high(-1e30f);bool visible=false;
    for(auto point:{Vector3(0),Vector3(-.16f,-.11f,-.3f),Vector3(.16f,-.11f,-.3f),Vector3(.16f,.11f,-.3f),Vector3(-.16f,.11f,-.3f),Vector3(0,.19f,-.3f)})
        if(auto p=GizmoMath::Project(Vector3(frame*Vector4(point,1)),origin,size,vp)){low=glm::min(low,Vector2(*p));high=glm::max(high,Vector2(*p));visible=true;}
    return visible&&glm::all(glm::greaterThanEqual(mouse,low-Vector2(6)))&&glm::all(glm::lessThanEqual(mouse,high+Vector2(6)));
}
inline std::vector<std::optional<Vector3>> ProjectVisible(const ShapeMesh& mesh,const ShapeSelection& selection,const MeshIntersection& spatial,
    Vector3 camera,Vector2 origin,Vector2 size,const Matrix4& vp,Vector3 offset=Vector3(0)){
    RICE_BENCHMARK_FUNCTION();
    std::vector<std::optional<Vector3>> result(selection.order.size());auto [low,high]=mesh.Bounds();float epsilon=std::max(1e-5f,glm::length(high-low)*1e-5f);
    for(size_t row=0;row<selection.order.size();++row){auto vertex=mesh.positions[selection.order[row]];auto p=GizmoMath::Project(vertex-offset,origin,size,vp);
        if(p&&p->x>=origin.x&&p->y>=origin.y&&p->x<=origin.x+size.x&&p->y<=origin.y+size.y&&spatial.Visible(camera+offset,vertex,epsilon))result[row]=p;
    }
    return result;
}
inline void Translate(ShapeMesh& mesh,const std::vector<Vector3>& start,const std::set<uint32_t>& selected,Vector3 delta){
    RICE_BENCHMARK_FUNCTION();
    for(auto index:selected)mesh.positions[index]=start[index]+delta;mesh.Touch();
}
inline void Rotate(ShapeMesh& mesh,const std::vector<Vector3>& start,uint32_t pivot,int axis,float degrees){
    RICE_BENCHMARK_FUNCTION();
    Vector3 normal(0);normal[axis]=1;auto rotation=glm::angleAxis(glm::radians(degrees),normal);
    for(size_t i=0;i<start.size();++i)mesh.positions[i]=start[pivot]+rotation*(start[i]-start[pivot]);mesh.Touch();
}
}
}
