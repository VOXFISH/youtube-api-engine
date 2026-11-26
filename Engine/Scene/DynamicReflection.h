#pragma once
#include "Rice/Benchmark.h"
#include "Scene.h"
#include "CubeCapture.h"
#include "Renderer/PerspectiveCamera.h"
#include <glad/glad.h>
#include <functional>
namespace Rice {
struct ReflectionProbe : Component {
    int resolution = 256;
    int storageQuality = 0; // 0: RGB8, 1: RGB16F
    float maxObjectSize = 100.f; // Flat-reflection threshold in viewport pixels.
    bool fixEdgeSeams = true;
};
// Project a local bounding box, clipping its edges to the near/far planes.
// Clamp to the viewport so off-screen portions do not count as displayed pixels.
inline float ReflectionPixelSize(const Matrix4& world,const Matrix4& viewProjection,
                                 Vector2 viewport,Vector3 low=Vector3(-.5f),Vector3 high=Vector3(.5f)){
    RICE_BENCHMARK_FUNCTION();
    Vector4 corners[8];
    for(int i=0;i<8;++i){
        Vector3 point;
        for(int axis=0;axis<3;++axis)point[axis]=(i&(1<<axis))?high[axis]:low[axis];
        corners[i]=viewProjection*world*Vector4(point,1);
    }
    Vector2 minimum(1.f),maximum(-1.f);
    bool visible=false;
    auto include=[&](Vector4 point){
        RICE_BENCHMARK_FUNCTION();
        if(point.w<=0.000001f)return;
        Vector2 ndc=glm::clamp(Vector2(point)/point.w,Vector2(-1),Vector2(1));
        minimum=glm::min(minimum,ndc);maximum=glm::max(maximum,ndc);visible=true;
    };
    for(int i=0;i<8;++i)for(int axis=0;axis<3;++axis){
        if(i&(1<<axis))continue;
        Vector4 a=corners[i],b=corners[i|(1<<axis)];
        bool clipped=false;
        for(float sign:{1.f,-1.f}){
            float da=a.w+sign*a.z,db=b.w+sign*b.z;
            if(da<0 && db<0){clipped=true;break;}
            if(da<0 || db<0){
                Vector4 intersection=a+(b-a)*(da/(da-db));
                if(da<0)a=intersection;else b=intersection;
            }
        }
        if(!clipped){include(a);include(b);}
    }
    if(!visible||glm::any(glm::lessThanEqual(maximum,minimum)))return 0.f;
    auto pixels=(maximum-minimum)*glm::max(viewport,Vector2(0))*.5f;
    return std::max(pixels.x,pixels.y);
}
class ReflectionCubemap {
    GLuint texture=0, framebuffer=0, depth=0;
    int allocatedResolution=0, allocatedQuality=-1;
    class CaptureCamera : public PerspectiveCamera {
        Matrix4 view, vp;
    public:
        CaptureCamera(Vector3 position,Vector3 direction,Vector3 up)
            : PerspectiveCamera(90,1,.05f,1000) {
            RICE_BENCHMARK_FUNCTION();
            SetPosition(position);
            view=glm::lookAt(position,position+direction,up);
            vp=GetProjectionMatrix()*view;
        }
        const Matrix4& GetViewMatrix()const override{ RICE_BENCHMARK_FUNCTION(); return view;}
        const Matrix4& GetVPMatrix()const override{ RICE_BENCHMARK_FUNCTION(); return vp;}
    };
public:
    ReflectionCubemap()=default;
    ReflectionCubemap(const ReflectionCubemap&)=delete;
    ReflectionCubemap& operator=(const ReflectionCubemap&)=delete;
    ~ReflectionCubemap(){
        RICE_BENCHMARK_FUNCTION();
        if(depth)glDeleteRenderbuffers(1,&depth);
        if(framebuffer)glDeleteFramebuffers(1,&framebuffer);
        if(texture)glDeleteTextures(1,&texture);
    }
    int MipLevels()const{ RICE_BENCHMARK_FUNCTION(); return allocatedResolution?int(std::log2(allocatedResolution))+1:0;}
    GLuint Texture()const{ RICE_BENCHMARK_FUNCTION(); return texture;}
    void Capture(Vector3 position,int resolution,int storageQuality,const std::function<void(const Camera&)>& draw){
        RICE_BENCHMARK_FUNCTION();
        GLint hardwareLimit=0;
        glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE,&hardwareLimit);
        int size=std::clamp(resolution,16,std::min(hardwareLimit,2048));
        int quality=std::clamp(storageQuality,0,1);
        GLint oldDraw,oldRead,viewport[4],oldActive,oldCube,oldDepth;
        GLfloat clearColor[4];
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldDraw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);
        glGetIntegerv(GL_VIEWPORT,viewport);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&oldCube);
        glGetIntegerv(GL_RENDERBUFFER_BINDING,&oldDepth);
        glGetFloatv(GL_COLOR_CLEAR_VALUE,clearColor);
        bool scissor=glIsEnabled(GL_SCISSOR_TEST),depthTest=glIsEnabled(GL_DEPTH_TEST);
        GLboolean depthWrite;glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);
        auto restore=[&]{
            RICE_BENCHMARK_FUNCTION();
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,oldDraw);glBindFramebuffer(GL_READ_FRAMEBUFFER,oldRead);
            glBindRenderbuffer(GL_RENDERBUFFER,oldDepth);
            glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
            glClearColor(clearColor[0],clearColor[1],clearColor[2],clearColor[3]);
            glDepthMask(depthWrite);
            if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
            if(depthTest)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_CUBE_MAP,oldCube);glActiveTexture(oldActive);
        };
        try{
            if(!texture){glGenTextures(1,&texture);glGenFramebuffers(1,&framebuffer);glGenRenderbuffers(1,&depth);}
            glBindTexture(GL_TEXTURE_CUBE_MAP,texture);
            glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
            if(size!=allocatedResolution || quality!=allocatedQuality){
                for(int face=0;face<6;++face)
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,quality?GL_RGB16F:GL_RGB8,size,size,0,GL_RGB,GL_FLOAT,nullptr);
                glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
                glBindRenderbuffer(GL_RENDERBUFFER,depth);
                glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,size,size);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
                allocatedResolution=size;allocatedQuality=quality;
            }
            glDisable(GL_SCISSOR_TEST);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
            glViewport(0,0,size,size);glClearColor(0,0,0,1);
            glBindTexture(GL_TEXTURE_CUBE_MAP,0);
            for(int face=0;face<6;++face){
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,texture,0);
                if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Reflection framebuffer incomplete");
                glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                CaptureCamera camera(position,CubeCaptureDirections[face],CubeCaptureUps[face]);
                draw(camera);
            }
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_CUBE_MAP,texture);
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
            restore();
        }catch(...){restore();throw;}
    }
};

class DynamicReflection {
    ReflectionCubemap cubemap;
    GLuint geometryBuffer=0,geometryTexture=0;
    std::vector<EntityID> geometryOwners;
    Vector3 capturePosition{0};
    int requestedResolution=0,requestedQuality=-1;
    bool capturedWithGeometry=false;
public:
    DynamicReflection()=default;
    DynamicReflection(const DynamicReflection&)=delete;
    DynamicReflection& operator=(const DynamicReflection&)=delete;
    ~DynamicReflection(){
        RICE_BENCHMARK_FUNCTION();
        if(geometryTexture)glDeleteTextures(1,&geometryTexture);
        if(geometryBuffer)glDeleteBuffers(1,&geometryBuffer);
    }
    int MipLevels()const{return cubemap.MipLevels();}
    GLuint Texture()const{return cubemap.Texture();}
    GLuint GeometryTexture()const{ RICE_BENCHMARK_FUNCTION(); return geometryTexture;}
    int GeometryGroupCount()const{ RICE_BENCHMARK_FUNCTION(); return static_cast<int>(geometryOwners.size());}
    const Vector3& CapturePosition()const{ RICE_BENCHMARK_FUNCTION(); return capturePosition;}
    int ReceiverIndex(EntityID id)const{
        RICE_BENCHMARK_FUNCTION();
        auto found=std::find(geometryOwners.begin(),geometryOwners.end(),id);
        return found==geometryOwners.end()?-1:static_cast<int>(found-geometryOwners.begin());
    }
    void SetGeometry(const std::vector<Vector4>& groups,const std::vector<Vector4>& payload,const std::vector<EntityID>& owners){
        RICE_BENCHMARK_FUNCTION();
        GLint limit=0;
        glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&limit);
        const size_t texels=groups.size()+payload.size();
        if(texels>static_cast<size_t>(limit))throw std::runtime_error("Reflection geometry exceeds GPU texture buffer capacity");
        GLint oldActive,oldTexture,oldBuffer;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
        glActiveTexture(GL_TEXTURE2);
        glGetIntegerv(GL_TEXTURE_BINDING_BUFFER,&oldTexture);
        glGetIntegerv(GL_TEXTURE_BUFFER_BINDING,&oldBuffer);
        if(!geometryBuffer){glGenBuffers(1,&geometryBuffer);glGenTextures(1,&geometryTexture);}
        glBindBuffer(GL_TEXTURE_BUFFER,geometryBuffer);
        glBufferData(GL_TEXTURE_BUFFER,std::max(texels,size_t(1))*sizeof(Vector4),nullptr,GL_DYNAMIC_DRAW);
        if(!groups.empty())glBufferSubData(GL_TEXTURE_BUFFER,0,groups.size()*sizeof(Vector4),groups.data());
        if(!payload.empty())glBufferSubData(GL_TEXTURE_BUFFER,groups.size()*sizeof(Vector4),payload.size()*sizeof(Vector4),payload.data());
        glBindTexture(GL_TEXTURE_BUFFER,geometryTexture);
        glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,geometryBuffer);
        glBindBuffer(GL_TEXTURE_BUFFER,oldBuffer);
        glBindTexture(GL_TEXTURE_BUFFER,oldTexture);glActiveTexture(oldActive);
        geometryOwners=owners;
    }
    bool SettingsChanged(const ReflectionProbe& probe,bool precise)const{
        RICE_BENCHMARK_FUNCTION();
        return !cubemap.Texture() || requestedResolution!=probe.resolution || requestedQuality!=probe.storageQuality ||
               (precise&&!capturedWithGeometry);
    }
    void Capture(ReflectionProbe& probe,bool precise,const std::function<void(const Camera&)>& draw){
        RICE_BENCHMARK_FUNCTION();
        auto position=Vector3(probe.GetGameObject().GetWorldMatrix()[3]);
        cubemap.Capture(position,probe.resolution,probe.storageQuality,draw);
        requestedResolution=probe.resolution;requestedQuality=probe.storageQuality;
        capturePosition=position;
        capturedWithGeometry=precise;
    }
};
}
