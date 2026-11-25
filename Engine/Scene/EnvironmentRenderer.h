#pragma once
#include "Scene.h"
#include "ShadowMaps.h"
#include "Renderer/Camera.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Renderer/RenderCommand.h"
#include <glad/glad.h>
namespace Rice {
// Screen-space ray intersections keep the ground and editor guides independent of camera distance.
class EnvironmentRenderer {
    Ref<Shader> shader;
public:
    void Draw(Scene& scene,const Camera& camera,const Ref<VertexArray>& quad,bool guides,const ShadowMaps* shadows=nullptr){
        const auto& s=scene.settings;
        bool sky=s.skybox&&scene.skyboxTexture;
        if(!sky&&!s.ground&&!guides)return;
        if(!shader)shader=Ref<Shader>(Shader::Create(R"(#version 330 core
layout(location=0) in vec3 position;
out vec2 screen;
void main(){screen=position.xy*2.0;gl_Position=vec4(screen,0,1);}
)",std::string(R"(#version 330 core
)")+ShadowVisibilityGLSL+R"(
in vec2 screen;out vec4 result;
uniform mat4 inverseVP,viewProjection;
uniform int skyEnabled,groundEnabled,skyLayout;
uniform vec3 axes,grids;
uniform sampler2D skyImage;
uniform int groundLightCount,groundLightType[8];
uniform vec3 groundLightColor[8],groundLightDirection[8];
uniform vec4 groundLightPositionRange[8];
float groundVisibility(vec3 position){
    // Preserve the established neutral ground as a shadow catcher. Ambient and
    // unoccluded lights remain visible, and no-light scenes retain the old look.
    float total=.15,visible=.15;
    for(int i=0;i<groundLightCount;++i){
        vec3 light=-groundLightDirection[i];float attenuation=1.;
        if(groundLightType[i]==0){
            vec3 offset=groundLightPositionRange[i].xyz-position;float distance=length(offset);
            light=offset/max(distance,.0001);
            float falloff=max(1.-distance/max(groundLightPositionRange[i].w,.001),0.);
            attenuation=falloff*falloff;
        }
        float weight=dot(max(groundLightColor[i],vec3(0)),vec3(.2126,.7152,.0722))*max(light.y,0.)*attenuation;
        total+=weight;visible+=weight*shadowVisibility(i,position,vec3(0,1,0),uvec2(0));
    }
    return visible/total;
}
vec2 skyUV(vec3 d){
    if(skyLayout==0)return vec2(atan(d.z,d.x)/6.2831853+.5,asin(clamp(d.y,-1.,1.))/3.14159265+.5);
    vec3 a=abs(d);int face;vec2 uv;
    if(a.x>=a.y&&a.x>=a.z){face=d.x>0?0:1;uv=vec2(d.x>0?-d.z:d.z,d.y)/a.x;}
    else if(a.y>=a.z){face=d.y>0?2:3;uv=vec2(d.x,d.y>0?-d.z:d.z)/a.y;}
    else{face=d.z>0?4:5;uv=vec2(d.z>0?d.x:-d.x,d.y)/a.z;}
    uv=uv*.5+.5;vec2 cell,size;
    if(skyLayout==1){size=vec2(3,4);cell=face==0?vec2(1,1):face==1?vec2(1,3):face==2?vec2(1,0):face==3?vec2(1,2):face==4?vec2(0,1):vec2(2,1);}
    else if(skyLayout==2){size=vec2(4,3);cell=face==0?vec2(2,1):face==1?vec2(0,1):face==2?vec2(1,0):face==3?vec2(1,2):face==4?vec2(1,1):vec2(3,1);}
    else{size=skyLayout==3?vec2(6,1):vec2(1,6);cell=skyLayout==3?vec2(face,0):vec2(0,face);}
    cell.y=size.y-1.-cell.y;vec2 pixels=vec2(textureSize(skyImage,0))/size;
    return (cell+clamp(uv,0.,1.)*(1.-1./pixels)+.5/pixels)/size;
}
float depthAt(vec3 p){vec4 clip=viewProjection*vec4(p,1);return clip.z/clip.w*.5+.5;}
void main(){
    vec4 a=inverseVP*vec4(screen,-1,1),b=inverseVP*vec4(screen,1,1);
    vec3 origin=a.xyz/a.w,end=b.xyz/b.w,ray=end-origin;
    vec3 color=skyEnabled==1?texture(skyImage,skyUV(normalize(ray))).rgb:vec3(.075,.08,.09);
    float best=1.;float depth=1.;bool visible=skyEnabled==1;
    if(groundEnabled==1&&abs(ray.y)>.000001){
        float t=-origin.y/ray.y;
        if(t>=0.&&t<=1.){vec3 point=origin+ray*t;best=t;depth=depthAt(point);color=vec3(.72*groundVisibility(point));visible=true;}
    }
    // XY, YZ and ZX planes. Axis colors: X red, Y green, Z blue.
    for(int plane=0;plane<3;++plane){
        int normalAxis=plane==0?2:plane==1?0:1;
        int u=plane==0?0:plane==1?1:2,v=plane==0?1:plane==1?2:0;
        float denominator=ray[normalAxis];
        float t=-origin[normalAxis]/(abs(denominator)>.000001?denominator:.000001);
        vec3 p=origin+ray*t;vec2 coord=vec2(p[u],p[v]);
        vec2 pixel=max(fwidth(coord),vec2(.0001));
        vec2 distanceToLine=abs(fract(coord+.5)-.5)/pixel;
        float grid=(1.-min(min(distanceToLine.x,distanceToLine.y),1.))*grids[plane];
        grid*=1.-smoothstep(.2,1.,max(pixel.x,pixel.y));
        vec3 line=vec3(.36);float alpha=grid*.5;
        for(int k=0;k<2;++k){
            int axis=k==0?u:v;float distanceToAxis=abs(coord[1-k])/pixel[1-k];
            float strength=(1.-smoothstep(.7,1.7,distanceToAxis))*axes[axis];
            if(strength>alpha){alpha=strength;line=axis==0?vec3(.9,.2,.2):axis==1?vec3(.2,.8,.3):vec3(.25,.45,1.);}
        }
        if(abs(denominator)>.000001&&t>=0.&&t<=best+.000001&&alpha>.01){
            color=mix(color,line,alpha);best=t;depth=depthAt(p);visible=true;
        }
    }
    if(!visible)discard;
    result=vec4(color,1);gl_FragDepth=clamp(depth,0.,1.);
}
)"));
        GLint depthFunction;GLboolean depthWrite;glGetIntegerv(GL_DEPTH_FUNC,&depthFunction);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);
        bool depthTest=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);glDisable(GL_CULL_FACE);
        shader->Bind();shader->SetUniformMat4("inverseVP",glm::inverse(camera.GetVPMatrix()));shader->SetUniformMat4("viewProjection",camera.GetVPMatrix());
        if(shadows){
            shadows->Bind(shader);
            const auto& lights=shadows->Lights();
            const int count=static_cast<int>(std::min(lights.size(),size_t(8)));
            shader->SetUniformInt("groundLightCount",count);
            for(int i=0;i<count;++i){
                auto suffix="["+std::to_string(i)+"]";
                shader->SetUniformInt("groundLightType"+suffix,lights[i].type);
                shader->SetUniformFloat3("groundLightColor"+suffix,lights[i].color);
                shader->SetUniformFloat3("groundLightDirection"+suffix,lights[i].direction);
                shader->SetUniformFloat4("groundLightPositionRange"+suffix,Vector4(lights[i].position,lights[i].range));
            }
        }else{
            ShadowMaps::SetSamplerUnits(shader);
            shader->SetUniformInt("shadowLightCount",0);shader->SetUniformInt("groundLightCount",0);
        }
        shader->SetUniformInt("skyEnabled",sky);shader->SetUniformInt("groundEnabled",s.ground);shader->SetUniformInt("skyImage",0);
        shader->SetUniformFloat3("axes",guides?s.axes:Vector3(0));shader->SetUniformFloat3("grids",guides?s.grids:Vector3(0));
        if(sky){scene.skyboxTexture->Bind(0);shader->SetUniformInt("skyLayout",int(DetectCubemapLayout(scene.skyboxTexture->GetWidth(),scene.skyboxTexture->GetHeight())));}
        RenderCommand::DrawIndexed(quad);
        glDepthFunc(depthFunction);glDepthMask(depthWrite);if(!depthTest)glDisable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);
    }
};
}
