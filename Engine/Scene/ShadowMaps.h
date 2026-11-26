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
uniform mat4 shadowMovingVP[8];
uniform vec2 shadowMovingLayerTexel[8];
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
// A quadratic B-spline gives adjacent texels continuous weights as the
// receiver crosses the grid, using the same nine owner-aware comparisons.
vec3 shadowWeights(float fraction){
    float left=.5*(1.-fraction)*(1.-fraction),right=.5*fraction*fraction;
    return vec3(left,1.-left-right,right);
}
float filteredShadow(vec2 uv,float receiverDepth,int layer,uvec2 receiver){
    vec2 grid=uv*shadowResolution;
    ivec2 start=ivec2(floor(grid))-ivec2(1);
    ivec2 last=ivec2(int(shadowResolution)-1);
    vec3 wx=shadowWeights(fract(grid.x)),wy=shadowWeights(fract(grid.y));
    float occlusion=0.;
    for(int y=0;y<3;++y)for(int x=0;x<3;++x){
        ivec3 tap=ivec3(clamp(start+ivec2(x,y),ivec2(0),last),layer);
        if(receiver!=uvec2(0) && texelFetch(shadowOwnerMap,tap,0).rg==receiver)
            tap.z+=shadowFaceCount;
        float nearest=texelFetch(shadowMap,tap,0).r;
        if(receiverDepth>nearest)occlusion+=wx[x]*wy[y];
    }
    return max(1.-occlusion,0.);
}
float directionalShadow(mat4 vp,int layer,float worldTexel,vec3 position,vec3 n,float bias,uvec2 receiver){
    vec4 clip=vp*vec4(position+n*worldTexel*.5,1.);
    vec3 uvDepth=clip.xyz/clip.w*.5+.5;
    if(uvDepth.z<0.||any(lessThan(uvDepth.xy,vec2(0)))||any(greaterThan(uvDepth.xy,vec2(1))))return 1.;
    // A receiver downstream of this caster volume can still be shadowed.
    return filteredShadow(uvDepth.xy,min(uvDepth.z,1.)-bias,layer,receiver);
}
float shadowVisibility(int i,vec3 position,vec3 normal,uvec2 receiver){
    if(i<0||i>=shadowLightCount||shadowKind[i]<0)return 1.;
    vec3 n=normalize(normal);
    if(shadowKind[i]==1){
        float bias=.00008+.0004*(1.-max(dot(n,-shadowDirection[i]),0.));
        float visibility=directionalShadow(shadowDirectionalVP[i],shadowLayer[i],shadowNearTexel[i].y,position,n,bias,receiver);
        vec2 moving=shadowMovingLayerTexel[i];
        if(moving.x>=0.)visibility=min(visibility,directionalShadow(shadowMovingVP[i],int(moving.x),moving.y,position,n,bias,receiver));
        return visibility;
    }
    vec3 offset=position-shadowPositionRange[i].xyz;
    float distance=length(offset),farPlane=shadowPositionRange[i].w;
    if(distance>=farPlane||distance<.000001)return 1.;
    // Scale the normal offset with the point-shadow texel footprint.
    float normalOffset=max(.003,3.*distance/max(shadowResolution,1.));
    int face;vec3 uvDepth=shadowPointProjection(offset+n*normalOffset,face);
    float nearPlane=shadowNearTexel[i].x;
    if(uvDepth.z<=nearPlane)return 1.;
    float depth=farPlane/(farPlane-nearPlane)-farPlane*nearPlane/((farPlane-nearPlane)*uvDepth.z);
    float bias=.000002+.00001*(1.-max(dot(n,-offset/distance),0.));
    return filteredShadow(uvDepth.xy,depth-bias,shadowLayer[i]+face,receiver);
}
)";
inline const char* ShadowVisibilityGLSL=ShadowSamplingGLSL;

struct DirectionalShadowGroup {
    std::vector<SpatialBounds> bounds,changes;
    uint64_t revision=0;
};

class ShadowMaps {
public:
    struct Stats {size_t updates=0,faces=0;};
private:
    struct CachedFace {
        bool valid=false;
        EntityID id=0;
        uint64_t scene=0,casters=0;
        int group=-1,face=0;
        Matrix4 vp{1};
        Vector3 direction{0},position{0};
        float texel=0,range=0;
    };
    struct ShadowView {
        CachedFace next;
        int light=0;
        bool dirty=false,partial=false;
    };
    struct PixelRegion {
        int left,bottom,right,top;
        Matrix4 CullMatrix(const Matrix4& vp,int resolution)const{
            float width=float(right-left),height=float(top-bottom);
            Matrix4 crop(1);
            crop[0][0]=resolution/width;crop[1][1]=resolution/height;
            crop[3][0]=(resolution-left-right)/width;crop[3][1]=(resolution-bottom-top)/height;
            return crop*vp;
        }
    };
    GLuint texture=0,owners=0,framebuffer=0;
    int requestedResolution=512,allocatedResolution=0,allocatedLayers=0,hardwareResolutionLimit=0;
    std::vector<ShadowLight> lights;
    std::array<CachedFace,48> cache{};
    std::array<int,8> kinds{},layers{},movingLayers{};
    std::array<Matrix4,8> directionalVP{},movingVP{};
    std::array<float,8> worldTexel{},movingTexel{},nearPlanes{};
    Stats stats;
    bool cachingEnabled=true;

    static Vector3 Direction(Vector3 value){return glm::length(value)>.00001f?glm::normalize(value):Vector3(0,-1,0);}
    static Matrix4 DirectionalMatrix(const std::vector<SpatialBounds>& casters,Vector3 direction,int resolution,float& texel){
        // Use fixed world-space light axes. An orthographic shadow ray stays at
        // the same light-plane XY from a caster all the way to its receiver.
        Vector3 up=std::abs(direction.y)>.99f?Vector3(0,0,1):Vector3(0,1,0);
        Matrix4 view=glm::lookAt(Vector3(0),direction,up);
        Vector3 minimum(1e30f),maximum(-1e30f);
        for(const auto& bounds:casters){
            auto projected=SpatialBounds::Transform(view,bounds);
            minimum=glm::min(minimum,projected.low);maximum=glm::max(maximum,projected.high);
        }
        if(casters.empty()){minimum=Vector3(-1);maximum=Vector3(1);}
        // Quantize scale in quarter-octaves and center to whole texels. A
        // translation can then reuse the same world-aligned sampling grid.
        Vector3 span=glm::max(maximum-minimum,Vector3(.01f));
        float padded=std::max(span.x,span.y)*resolution/float(resolution-6);
        float width=std::exp2(std::ceil(std::log2(padded)*4.f)*.25f);
        texel=width/resolution;
        Vector2 center=glm::round(Vector2((minimum+maximum)*.5f)/texel)*texel;
        float depthStep=std::max(texel,span.z*.05f);
        float nearClip=-std::ceil(maximum.z/depthStep+1)*depthStep;
        float farClip=-std::floor(minimum.z/depthStep-1)*depthStep;
        return glm::ortho(center.x-width*.5f,center.x+width*.5f,center.y-width*.5f,center.y+width*.5f,nearClip,farClip)*view;
    }
    static PixelRegion ChangedRegion(const std::vector<SpatialBounds>& changes,const Matrix4& vp,int resolution){
        Vector2 low(1),high(-1);
        for(const auto& bounds:changes){
            auto clip=SpatialBounds::Transform(vp,bounds);
            low=glm::min(low,Vector2(clip.low));high=glm::max(high,Vector2(clip.high));
        }
        // One extra pixel for rasterization rounding at each edge.
        Vector2 lo=glm::clamp(glm::floor((low*.5f+.5f)*float(resolution))-1.f,Vector2(0),Vector2(resolution));
        Vector2 hi=glm::clamp(glm::ceil((high*.5f+.5f)*float(resolution))+1.f,Vector2(0),Vector2(resolution));
        return {int(lo.x),int(lo.y),int(hi.x),int(hi.y)};
    }
    // The callback changes shader/VAO state; retain those bindings as well as all
    // depth-pass state here so editor rendering and nested reflection views agree.
    struct State {
        GLint draw=0,read=0,viewport[4]{},scissorBox[4]{},active=0,array=0,ownerArray=0,image=0,program=0,vao=0,buffer=0,depthFunction=0,polygonMode[2]{};
        GLboolean depthWrite=GL_TRUE,colorWrite[4]{};
        GLdouble clearDepth=1;
        bool scissor=false,depth=false,blend=false,cull=false,offset=false,discard=false,depthClamp=false,logicOp=false;
        State(){
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
            glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
            glGetIntegerv(GL_SCISSOR_BOX,scissorBox);
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
            glScissor(scissorBox[0],scissorBox[1],scissorBox[2],scissorBox[3]);
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
    void AllocateMaps(int resolution,int faces){
        // Some drivers retain uninitialized depth tiles after per-layer FBO clears.
        // Seed every nearest and alternate depth slice once, reusing one CPU slice.
        std::vector<float> farDepth(size_t(resolution)*resolution,1.f);
        constexpr std::array<GLenum,7> unpackParameters{GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_IMAGE_HEIGHT,
            GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_IMAGES,GL_UNPACK_SWAP_BYTES};
        std::array<GLint,unpackParameters.size()> previousUnpack{};
        GLint unpackBuffer=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackBuffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        for(size_t i=0;i<unpackParameters.size();++i){
            glGetIntegerv(unpackParameters[i],&previousUnpack[i]);
            glPixelStorei(unpackParameters[i],unpackParameters[i]==GL_UNPACK_ALIGNMENT?4:0);
        }
        if(!texture)glGenTextures(1,&texture);
        if(!owners)glGenTextures(1,&owners);
        AllocateArray(texture,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_FLOAT,resolution,2*faces);
        for(int layer=0;layer<2*faces;++layer)
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,layer,resolution,resolution,1,GL_DEPTH_COMPONENT,GL_FLOAT,farDepth.data());
        AllocateArray(owners,GL_RG32UI,GL_RG_INTEGER,GL_UNSIGNED_INT,resolution,faces);
        for(size_t i=0;i<unpackParameters.size();++i)glPixelStorei(unpackParameters[i],previousUnpack[i]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpackBuffer);
    }
public:
    ShadowMaps(){kinds.fill(-1);layers.fill(-1);movingLayers.fill(-1);directionalVP.fill(Matrix4(1));movingVP.fill(Matrix4(1));}
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
    template<class Draw> void Update(uint64_t sceneID,const std::vector<ShadowLight>& source,const std::array<DirectionalShadowGroup,2>& groups,const std::array<uint64_t,8>& casterRevisions,Draw draw){
        stats={};lights.assign(source.begin(),source.begin()+std::min(source.size(),size_t(8)));
        kinds.fill(-1);layers.fill(-1);movingLayers.fill(-1);int layerCount=0;
        const bool split=!groups[0].bounds.empty()&&!groups[1].bounds.empty();
        std::array<ShadowView,48> views;
        for(size_t i=0;i<lights.size();++i){
            auto& light=lights[i];light.direction=Direction(light.direction);light.range=std::max(light.range,.001f);
            if(!light.castsShadow)continue;
            kinds[i]=light.type==1?1:0;layers[i]=layerCount;
            int faces=kinds[i]==1?(split?2:1):6;
            if(kinds[i]==1&&split)movingLayers[i]=layerCount+1;
            nearPlanes[i]=std::min(.05f,light.range*.01f);
            for(int face=0;face<faces;++face){
                auto& view=views[layerCount++];
                view.light=static_cast<int>(i);
                auto& next=view.next;
                next.id=light.id;next.scene=sceneID;next.valid=true;next.face=face;
                if(kinds[i]==1){
                    next.group=(face==1||(groups[0].bounds.empty()&&!groups[1].bounds.empty()))?1:0;
                    next.casters=groups[next.group].revision;next.direction=light.direction;
                }else{
                    next.casters=casterRevisions[i];next.position=light.position;next.range=light.range;
                }
            }
        }
        if(!layerCount){for(auto& value:cache)value.valid=false;return;}
        if(!hardwareResolutionLimit)glGetIntegerv(GL_MAX_TEXTURE_SIZE,&hardwareResolutionLimit);
        const int resolution=std::min(requestedResolution,hardwareResolutionLimit);
        const bool reallocate=allocatedResolution!=resolution||allocatedLayers!=layerCount;
        bool anyDirty=false;
        for(int layer=0;layer<layerCount;++layer){
            auto& view=views[layer];auto& next=view.next;const auto& previous=cache[layer];
            bool same=!reallocate&&previous.valid&&previous.scene==sceneID&&previous.id==next.id&&previous.group==next.group&&previous.face==next.face;
            if(next.group>=0){
                same=same&&next.direction==previous.direction;
                if(same&&next.casters==previous.casters){next.vp=previous.vp;next.texel=previous.texel;}
                else next.vp=DirectionalMatrix(groups[next.group].bounds,next.direction,resolution,next.texel);
                if(layer==movingLayers[view.light]){movingVP[view.light]=next.vp;movingTexel[view.light]=next.texel;}
                else{directionalVP[view.light]=next.vp;worldTexel[view.light]=next.texel;}
                same=same&&previous.vp==next.vp;
            }else{
                same=same&&previous.position==next.position&&previous.range==next.range;
                if(same)next.vp=previous.vp;
                else next.vp=glm::perspective(glm::radians(90.f),1.f,nearPlanes[view.light],next.range)*CubeCaptureView(next.position,next.face);
            }
            view.dirty=!cachingEnabled||!same||previous.casters!=next.casters;
            view.partial=cachingEnabled&&same&&next.group>=0&&previous.casters+1==next.casters&&!groups[next.group].changes.empty();
            anyDirty=anyDirty||view.dirty;
        }
        // Static views only update CPU light metadata. No GL state queries,
        // framebuffer switches or texture binds are needed on a cache hit.
        if(!anyDirty)return;
        State state;
        if(!framebuffer)glGenFramebuffers(1,&framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glActiveTexture(GL_TEXTURE3);
        if(reallocate){
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,0,0,0);
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0,0,0);
            AllocateMaps(resolution,layerCount);
            allocatedResolution=resolution;allocatedLayers=layerCount;for(auto& value:cache)value.valid=false;
        }
        // Avoid a feedback binding while the same texture is attached for writing.
        glBindTexture(GL_TEXTURE_2D_ARRAY,0);
        glReadBuffer(GL_NONE);
        glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_COLOR_LOGIC_OP);
        glViewport(0,0,resolution,resolution);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_RASTERIZER_DISCARD);glDisable(GL_DEPTH_CLAMP);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glClearDepth(1);
        int lastLight=-1;
        for(int layer=0;layer<layerCount;++layer){
            const auto& view=views[layer];
            if(!view.dirty)continue;
            const auto& next=view.next;
            Matrix4 cullVP=next.vp;
            if(view.partial){
                auto rect=ChangedRegion(groups[next.group].changes,next.vp,resolution);
                if(rect.left>=rect.right||rect.bottom>=rect.top){cache[layer]=next;continue;}
                glEnable(GL_SCISSOR_TEST);
                glScissor(rect.left,rect.bottom,rect.right-rect.left,rect.top-rect.bottom);
                cullVP=rect.CullMatrix(next.vp,resolution);
            }else glDisable(GL_SCISSOR_TEST);
            cache[layer].valid=false;
            glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D_ARRAY,0);
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,texture,0,layer);
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,owners,0,layer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Shadow framebuffer incomplete");
            constexpr GLuint noOwner[4]{};
            glClearBufferuiv(GL_COLOR,0,noOwner);
            glClear(GL_DEPTH_BUFFER_BIT);
            draw(next.vp,cullVP,layer,next.group,false);

            // Read the owner of this texel only after detaching the color target.
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0,0,0);
            glDrawBuffer(GL_NONE);
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,texture,0,layer+layerCount);
            glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D_ARRAY,owners);
            glClear(GL_DEPTH_BUFFER_BIT);
            draw(next.vp,cullVP,layer,next.group,true);
            ++stats.faces;
            if(lastLight!=view.light){++stats.updates;lastLight=view.light;}
            cache[layer]=next;
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
            shader->SetUniformMat4("shadowMovingVP"+index,movingVP[i]);
            shader->SetUniformFloat2("shadowMovingLayerTexel"+index,{float(movingLayers[i]),movingTexel[i]});
            shader->SetUniformFloat4("shadowPositionRange"+index,Vector4(lights[i].position,lights[i].range));
            shader->SetUniformFloat3("shadowDirection"+index,lights[i].direction);
            shader->SetUniformFloat2("shadowNearTexel"+index,{nearPlanes[i],worldTexel[i]});
        }
    }
};
}
