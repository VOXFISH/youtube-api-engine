#pragma once
#include "Scene.h"
#include "SpatialBVH.h"
#include "CubeCapture.h"
#include "Renderer/Shader.h"
#include <glad/glad.h>
#include <array>
#include <functional>

namespace Rice {
struct ShadowLight {
    EntityID id=0;
    int type=0; // 0: point, 1: directional; matches the forward-light array.
    Vector3 position{0},direction{0,-1,0},color{1};
    float range=10;
    bool castsShadow=true;
};

// All lights share depth and owner arrays. A point light has six consecutive
// faces in cubemap order and a directional light has one.
inline const char* ShadowSamplingGLSL=R"(
uniform sampler2DArray shadowMap;
uniform usampler2DArray shadowOwnerMap;
uniform int shadowFaceCount;
uniform int shadowLightCount;
uniform int shadowKind[8],shadowLayer[8];
uniform mat4 shadowDirectionalVP[8];
uniform vec4 shadowPositionRange[8];
uniform vec3 shadowDirection[8];
uniform vec2 shadowNearTexel[8];
uniform float shadowResolution;
vec3 shadowPointProjection(vec3 d,out int face){
    vec3 a=abs(d);vec2 uv;float z;
    if(a.x>=a.y&&a.x>=a.z){z=a.x;face=d.x>=0.?0:1;uv=vec2(d.x>=0.?-d.z:d.z,-d.y);}
    else if(a.y>=a.z){z=a.y;face=d.y>=0.?2:3;uv=vec2(d.x,d.y>=0.?d.z:-d.z);}
    else{z=a.z;face=d.z>=0.?4:5;uv=vec2(d.z>=0.?d.x:-d.x,-d.y);}
    return vec3(uv/max(z,.000001)*.5+.5,z);
}
float shadowVisibility(int i,vec3 position,vec3 normal,uvec2 receiver){
    if(i<0||i>=shadowLightCount||shadowKind[i]<0)return 1.;
    vec3 n=normalize(normal);vec3 L=-shadowDirection[i];
    vec3 uvDepth;int layer=shadowLayer[i];
    float bias;
    if(shadowKind[i]==1){
        vec3 p=position+n*shadowNearTexel[i].y*.5;
        vec4 clip=shadowDirectionalVP[i]*vec4(p,1);
        uvDepth=clip.xyz/clip.w*.5+.5;
        if(any(lessThan(uvDepth,vec3(0)))||any(greaterThan(uvDepth,vec3(1))))return 1.;
        bias=.00008+.0004*(1.-max(dot(n,L),0.));
    }else{
        vec3 offset=position-shadowPositionRange[i].xyz;
        float distance=length(offset),farPlane=shadowPositionRange[i].w;
        if(distance>=farPlane||distance<.000001)return 1.;
        L=-offset/distance;
        // A fixed world offset undersamples large/grazing point-shadow texels.
        // Scale the normal bias with the projected texel footprint instead.
        float normalOffset=max(.003,3.*distance/max(shadowResolution,1.));
        int face;uvDepth=shadowPointProjection(offset+n*normalOffset,face);layer+=face;
        float nearPlane=shadowNearTexel[i].x;
        if(uvDepth.z<=nearPlane)return 1.;
        uvDepth.z=farPlane/(farPlane-nearPlane)-farPlane*nearPlane/((farPlane-nearPlane)*uvDepth.z);
        bias=.000002+.00001*(1.-max(dot(n,L),0.));
    }
    float visibility=0.,texel=1./max(shadowResolution,1.);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec2 uv=clamp(uvDepth.xy+vec2(x,y)*texel,vec2(texel*.5),vec2(1.-texel*.5));
        ivec3 tap=ivec3(ivec2(uv*shadowResolution),layer);
        if(receiver!=uvec2(0) && texelFetch(shadowOwnerMap,tap,0).rg==receiver)
            tap.z+=shadowFaceCount;
        float nearest=texelFetch(shadowMap,tap,0).r;
        visibility+=uvDepth.z-bias<=nearest?1.:0.;
    }
    return visibility/9.;
}
)";
inline const char* ShadowVisibilityGLSL=ShadowSamplingGLSL;

class ShadowMaps {
public:
    struct Stats {size_t updates=0,faces=0;};
private:
    struct CachedLight {
        bool valid=false;
        EntityID id=0;
        uint64_t scene=0,casters=0;
        int kind=-1,layer=-1;
        Vector3 position{0},direction{0};
        float range=0;
        Matrix4 directionalVP{1};
    };
    GLuint texture=0,owners=0,framebuffer=0;
    int requestedResolution=512,allocatedResolution=0,allocatedLayers=0,hardwareResolutionLimit=0;
    std::vector<ShadowLight> lights;
    std::array<CachedLight,8> cache{};
    std::array<int,8> kinds{},layers{};
    std::array<Matrix4,8> directionalVP{};
    std::array<float,8> worldTexel{},nearPlanes{};
    Stats stats;
    bool cachingEnabled=true;

    static Vector3 Direction(Vector3 value){return glm::length(value)>.00001f?glm::normalize(value):Vector3(0,-1,0);}
    static Matrix4 DirectionalMatrix(const SpatialBounds& scene,Vector3 direction,int resolution,float& texel){
        std::vector<Vector3> points;points.reserve(16);
        Vector3 low=scene.low,high=scene.high;
        float extent=std::max(glm::length(high-low),1.f);
        // Include the y=0 receiver beneath casters. Near-horizontal lights get a
        // finite fitting distance rather than an unbounded orthographic volume.
        const float maxProjection=std::max(100.f,extent*4.f);
        for(int i=0;i<8;++i){
            Vector3 p(i&1?scene.high.x:scene.low.x,i&2?scene.high.y:scene.low.y,i&4?scene.high.z:scene.low.z);
            points.push_back(p);
            if(std::abs(direction.y)>.00001f){
                float t=-p.y/direction.y;
                if(t>=0){Vector3 projected=p+direction*std::min(t,maxProjection);points.push_back(projected);low=glm::min(low,projected);high=glm::max(high,projected);}
            }
        }
        Vector3 center=(low+high)*.5f;
        float radius=std::max(glm::length(high-low)*.5f,1.f);
        Vector3 up=std::abs(direction.y)>.99f?Vector3(0,0,1):Vector3(0,1,0);
        Matrix4 view=glm::lookAt(center-direction*(radius+10.f),center,up);
        Vector3 minimum(1e30f),maximum(-1e30f);
        for(auto p:points){Vector3 q(view*Vector4(p,1));minimum=glm::min(minimum,q);maximum=glm::max(maximum,q);}
        float padding=std::max(.1f,radius*.02f);
        minimum-=Vector3(padding);maximum+=Vector3(padding);
        texel=std::max(maximum.x-minimum.x,maximum.y-minimum.y)/float(resolution);
        float nearClip=std::max(.01f,-maximum.z),farClip=std::max(nearClip+.1f,-minimum.z);
        return glm::ortho(minimum.x,maximum.x,minimum.y,maximum.y,nearClip,farClip)*view;
    }
    // The callback changes shader/VAO state; retain those bindings as well as all
    // depth-pass state here so editor rendering and nested reflection views agree.
    struct State {
        GLint draw=0,read=0,viewport[4]{},active=0,array=0,ownerArray=0,image=0,program=0,vao=0,buffer=0,depthFunction=0,polygonMode[2]{};
        GLboolean depthWrite=GL_TRUE,colorWrite[4]{};
        GLdouble clearDepth=1;
        bool scissor=false,depth=false,blend=false,cull=false,offset=false,discard=false,depthClamp=false,logicOp=false;
        State(){
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
            glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
            glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&image);
            glActiveTexture(GL_TEXTURE3);glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&array);
            glActiveTexture(GL_TEXTURE4);glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&ownerArray);
            glGetBooleani_v(GL_COLOR_WRITEMASK,0,colorWrite);logicOp=glIsEnabled(GL_COLOR_LOGIC_OP);
            glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
            glGetIntegerv(GL_DEPTH_FUNC,&depthFunction);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);glGetDoublev(GL_DEPTH_CLEAR_VALUE,&clearDepth);
            scissor=glIsEnabled(GL_SCISSOR_TEST);depth=glIsEnabled(GL_DEPTH_TEST);blend=glIsEnabled(GL_BLEND);cull=glIsEnabled(GL_CULL_FACE);offset=glIsEnabled(GL_POLYGON_OFFSET_FILL);
            discard=glIsEnabled(GL_RASTERIZER_DISCARD);depthClamp=glIsEnabled(GL_DEPTH_CLAMP);glGetIntegerv(GL_POLYGON_MODE,polygonMode);
        }
        ~State(){
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);glBindFramebuffer(GL_READ_FRAMEBUFFER,read);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
            glDepthFunc(depthFunction);glDepthMask(depthWrite);glClearDepth(clearDepth);
            glColorMaski(0,colorWrite[0],colorWrite[1],colorWrite[2],colorWrite[3]);Set(GL_COLOR_LOGIC_OP,logicOp);
            Set(GL_SCISSOR_TEST,scissor);Set(GL_DEPTH_TEST,depth);Set(GL_BLEND,blend);Set(GL_CULL_FACE,cull);Set(GL_POLYGON_OFFSET_FILL,offset);
            Set(GL_RASTERIZER_DISCARD,discard);Set(GL_DEPTH_CLAMP,depthClamp);glPolygonMode(GL_FRONT_AND_BACK,polygonMode[0]);
            // A mesh upload may replace the previously bound VAO/buffer. A
            // deleted-but-current program may also disappear once draw binds
            // its shader. Such bindings can only be restored to zero safely.
            glUseProgram(program&&glIsProgram(program)?program:0);
            glBindVertexArray(vao&&glIsVertexArray(vao)?vao:0);
            glBindBuffer(GL_ARRAY_BUFFER,buffer&&glIsBuffer(buffer)?buffer:0);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,image);
            glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D_ARRAY,array);
            glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D_ARRAY,ownerArray);glActiveTexture(active);
        }
        static void Set(GLenum capability,bool enabled){if(enabled)glEnable(capability);else glDisable(capability);}
    };
    static void AllocateArray(GLuint name,GLint format,GLenum channels,GLenum type,int size,int layers){
        glBindTexture(GL_TEXTURE_2D_ARRAY,name);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,0,format,size,size,layers,0,channels,type,nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
public:
    ShadowMaps(){kinds.fill(-1);layers.fill(-1);directionalVP.fill(Matrix4(1));}
    ShadowMaps(const ShadowMaps&)=delete;
    ShadowMaps& operator=(const ShadowMaps&)=delete;
    ~ShadowMaps(){if(framebuffer)glDeleteFramebuffers(1,&framebuffer);if(texture)glDeleteTextures(1,&texture);if(owners)glDeleteTextures(1,&owners);}
    void SetResolution(int value){requestedResolution=std::clamp(value,64,2048);}
    void SetCachingEnabled(bool enabled){cachingEnabled=enabled;}
    int Resolution()const{return allocatedResolution?allocatedResolution:requestedResolution;}
    const Stats& GetStats()const{return stats;}
    const std::vector<ShadowLight>& Lights()const{return lights;}
    GLuint Texture()const{return texture;}
    static void SetSamplerUnits(const Ref<Shader>& shader){
        shader->SetUniformInt("shadowMap",3);
        shader->SetUniformInt("shadowOwnerMap",4);
    }
    template<class Draw> void Update(uint64_t sceneID,const std::vector<ShadowLight>& source,const SpatialBounds& sceneBounds,const std::array<uint64_t,8>& casterRevisions,Draw draw){
        stats={};lights.assign(source.begin(),source.begin()+std::min(source.size(),size_t(8)));
        kinds.fill(-1);layers.fill(-1);int layerCount=0;
        for(size_t i=0;i<lights.size();++i){
            auto& light=lights[i];light.direction=Direction(light.direction);light.range=std::max(light.range,.001f);
            if(!light.castsShadow)continue;
            kinds[i]=light.type==1?1:0;layers[i]=layerCount;layerCount+=kinds[i]==1?1:6;
        }
        if(!layerCount){for(auto& value:cache)value.valid=false;return;}
        if(!hardwareResolutionLimit)glGetIntegerv(GL_MAX_TEXTURE_SIZE,&hardwareResolutionLimit);
        const int resolution=std::min(requestedResolution,hardwareResolutionLimit);
        const bool reallocate=allocatedResolution!=resolution||allocatedLayers!=layerCount;
        std::array<bool,8> dirty{};bool anyDirty=false;
        for(size_t i=0;i<lights.size();++i){
            if(kinds[i]<0){cache[i].valid=false;continue;}
            const auto& light=lights[i];const auto& previous=cache[i];
            nearPlanes[i]=std::min(.05f,light.range*.01f);
            if(kinds[i]==1)directionalVP[i]=DirectionalMatrix(sceneBounds,light.direction,resolution,worldTexel[i]);
            else {directionalVP[i]=Matrix4(1);worldTexel[i]=0;}
            dirty[i]=!cachingEnabled||reallocate||!previous.valid||previous.scene!=sceneID||previous.casters!=casterRevisions[i]||previous.id!=light.id||previous.kind!=kinds[i]||previous.layer!=layers[i];
            if(kinds[i]==1)dirty[i]=dirty[i]||previous.direction!=light.direction||previous.directionalVP!=directionalVP[i];
            else dirty[i]=dirty[i]||previous.position!=light.position||previous.range!=light.range;
            anyDirty=anyDirty||dirty[i];
        }
        // Static views only update CPU light metadata. No GL state queries,
        // framebuffer switches or texture binds are needed on a cache hit.
        if(!anyDirty)return;
        State state;
        if(!texture)glGenTextures(1,&texture);
        if(!owners)glGenTextures(1,&owners);
        if(!framebuffer)glGenFramebuffers(1,&framebuffer);
        glActiveTexture(GL_TEXTURE3);
        if(reallocate){
            // Each face keeps two depths: the closest caster and the closest different caster.
            AllocateArray(texture,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_FLOAT,resolution,2*layerCount);
            AllocateArray(owners,GL_RG32UI,GL_RG_INTEGER,GL_UNSIGNED_INT,resolution,layerCount);
            allocatedResolution=resolution;allocatedLayers=layerCount;for(auto& value:cache)value.valid=false;
        }
        // Avoid a feedback binding while the same texture is attached for writing.
        glBindTexture(GL_TEXTURE_2D_ARRAY,0);
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glReadBuffer(GL_NONE);
        glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_COLOR_LOGIC_OP);
        glViewport(0,0,resolution,resolution);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_RASTERIZER_DISCARD);glDisable(GL_DEPTH_CLAMP);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glClearDepth(1);
        for(size_t i=0;i<lights.size();++i){
            if(!dirty[i])continue;
            const auto& light=lights[i];auto& previous=cache[i];
            previous.valid=false;
            for(int face=0;face<(kinds[i]==1?1:6);++face){
                const int layer=layers[i]+face;
                Matrix4 vp=directionalVP[i];
                if(kinds[i]==0)vp=glm::perspective(glm::radians(90.f),1.f,nearPlanes[i],light.range)*CubeCaptureView(light.position,face);
                glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D_ARRAY,0);
                glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,texture,0,layer);
                glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,owners,0,layer);
                glDrawBuffer(GL_COLOR_ATTACHMENT0);
                if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Shadow framebuffer incomplete");
                constexpr GLuint noOwner[4]{};
                glClearBufferuiv(GL_COLOR,0,noOwner);
                glClear(GL_DEPTH_BUFFER_BIT);
                draw(i,face,vp,layer,false);

                // Read the owner of this texel only after detaching the color target.
                glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0,0,0);
                glDrawBuffer(GL_NONE);
                glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,texture,0,layer+layerCount);
                glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D_ARRAY,owners);
                glClear(GL_DEPTH_BUFFER_BIT);
                draw(i,face,vp,layer,true);
                ++stats.faces;
            }
            previous={true,light.id,sceneID,casterRevisions[i],kinds[i],layers[i],light.position,light.direction,light.range,directionalVP[i]};++stats.updates;
        }
    }
    void Bind(const Ref<Shader>& shader)const{
        SetSamplerUnits(shader);
        shader->SetUniformInt("shadowFaceCount",allocatedLayers);shader->SetUniformInt("shadowLightCount",static_cast<int>(lights.size()));
        shader->SetUniformFloat("shadowResolution",float(Resolution()));
        GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D_ARRAY,texture);
        glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D_ARRAY,owners);glActiveTexture(active);
        for(size_t i=0;i<lights.size();++i){
            const auto index="["+std::to_string(i)+"]";
            shader->SetUniformInt("shadowKind"+index,kinds[i]);shader->SetUniformInt("shadowLayer"+index,layers[i]);
            shader->SetUniformMat4("shadowDirectionalVP"+index,directionalVP[i]);
            shader->SetUniformFloat4("shadowPositionRange"+index,Vector4(lights[i].position,lights[i].range));
            shader->SetUniformFloat3("shadowDirection"+index,lights[i].direction);
            shader->SetUniformFloat2("shadowNearTexel"+index,{nearPlanes[i],worldTexel[i]});
        }
    }
};
}
