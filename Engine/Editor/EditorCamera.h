#pragma once
#include "Rice/Benchmark.h"
#include "Renderer/PerspectiveCamera.h"
#include "Scene/Scene.h"
#include "Scene/SpatialBVH.h"
#include <algorithm>
#include <cmath>
namespace Rice {
// A middle-button click toggles mode only if it never becomes a drag.
class CameraWheelGesture {
    bool held=false,dragged=false;Vector2 start{0},previous{0};
public:
    struct Result {Vector2 orbitDelta{0};bool toggle=false,captured=false;};
    void Cancel(){ RICE_BENCHMARK_FUNCTION(); held=dragged=false;}
    Result Update(Vector2 mouse,bool pressed,bool down,bool allowed,bool cancel){
        RICE_BENCHMARK_FUNCTION();
        Result result;
        if(cancel){result.captured=held;Cancel();return result;}
        if(pressed&&allowed){held=true;dragged=false;start=previous=mouse;}
        if(!held)return result;result.captured=true;
        if(!dragged&&glm::length(mouse-start)>=4.f)dragged=true;
        if(dragged){result.orbitDelta=mouse-previous;previous=mouse;}
        if(!down){result.toggle=!dragged;Cancel();}
        return result;
    }
};
class CameraPointerGesture {
    int button=-1;bool dragged=false;Vector2 start{0},previous{0};
public:
    struct Result {Vector2 delta{0};int button=-1,click=-1;bool captured=false;};
    bool Active()const{ RICE_BENCHMARK_FUNCTION(); return button>=0;}
    void Cancel(){ RICE_BENCHMARK_FUNCTION(); button=-1;dragged=false;}
    Result Update(Vector2 mouse,int pressed,bool leftDown,bool rightDown,bool allowed,bool cancel){
        RICE_BENCHMARK_FUNCTION();
        Result result;
        if(cancel){result.captured=Active();Cancel();return result;}
        if(!Active()&&allowed&&pressed>=0){button=pressed;start=previous=mouse;dragged=false;}
        if(!Active())return result;result.captured=true;result.button=button;
        if(glm::length(mouse-start)>=4.f)dragged=true;
        if(dragged){result.delta=mouse-previous;previous=mouse;}
        if(!(button==0?leftDown:rightDown)){if(!dragged)result.click=button;Cancel();}
        return result;
    }
};
// Editor-only navigation state; never attached to a gameplay entity.
class EditorCamera {
    PerspectiveCamera camera{45,16.f/9,.1f,1000};
    float nearClip=.1f,farClip=1000;
public:
    Vector3 position{0,4,10};float yaw=-90,pitch=-21.801409f;
    Vector3 Forward()const{ RICE_BENCHMARK_FUNCTION(); float y=glm::radians(yaw),p=glm::radians(pitch);return glm::normalize(Vector3(std::cos(p)*std::cos(y),std::sin(p),std::cos(p)*std::sin(y)));}
    Vector3 Right()const{ RICE_BENCHMARK_FUNCTION(); return glm::normalize(glm::cross(Forward(),Vector3(0,1,0)));}
    Vector3 Up()const{ RICE_BENCHMARK_FUNCTION(); return glm::normalize(glm::cross(Right(),Forward()));}
    void Move(Vector3 input,float dt){ RICE_BENCHMARK_FUNCTION(); auto delta=Right()*input.x+Vector3(0,1,0)*input.y+Forward()*input.z;if(glm::length(delta)>1e-6f)position+=glm::normalize(delta)*5.f*std::clamp(dt,0.f,.1f);}
    void Zoom(float distance){ RICE_BENCHMARK_FUNCTION(); position+=Forward()*distance;}
    void Look(Vector2 pixels){ RICE_BENCHMARK_FUNCTION(); yaw=std::remainder(yaw+pixels.x*.2f,360.f);pitch=std::clamp(pitch-pixels.y*.2f,-89.f,89.f);}
    void Orbit(Vector3 pivot,Vector2 pixels){
        RICE_BENCHMARK_FUNCTION();
        float yawDelta=pixels.x*.2f;
        float nextPitch=std::clamp(pitch-pixels.y*.2f,-89.f,89.f);
        auto horizontal=glm::angleAxis(glm::radians(-yawDelta),Vector3(0,1,0));
        auto vertical=glm::angleAxis(glm::radians(nextPitch-pitch),horizontal*Right());
        position=pivot+(vertical*horizontal)*(position-pivot);
        yaw=std::remainder(yaw+yawDelta,360.f);pitch=nextPitch;
    }
    void Pan(Vector2 pixels,float height){ RICE_BENCHMARK_FUNCTION(); float units=2.f*10.f*std::tan(glm::radians(45.f)*.5f)/std::max(1.f,height);position+=(-Right()*pixels.x+Up()*pixels.y)*units;}
    void FrameHierarchy(Scene& scene,EntityID rootId){
        RICE_BENCHMARK_FUNCTION();
        auto* root=scene.Find(rootId);
        if(!root)return;
        std::optional<SpatialBounds> bounds;
        for(const auto& object:scene.GetObjects()){
            if(!object->IsActive()||!object->DependsOn(root))continue;
            object->EachComponent([&](Component& component){
                if(!component.enabled)return;
                auto local=component.GeometryBounds();
                if(!local)return;
                auto world=SpatialBounds::Transform(object->GetWorldMatrix(),{local->first,local->second});
                if(!bounds){bounds=world;return;}
                bounds->low=glm::min(bounds->low,world.low);
                bounds->high=glm::max(bounds->high,world.high);
            });
        }
        if(!bounds)return;
        const auto& projection=camera.GetProjectionMatrix();
        const float halfAngle=std::atan(1.f/std::max(projection[0][0],projection[1][1]));
        const float radius=std::max(glm::length(bounds->high-bounds->low)*.5f,1.e-5f);
        const float distance=1.1f*radius/std::sin(halfAngle);
        if(!std::isfinite(distance))return;
        position=(bounds->low+bounds->high)*.5f-Forward()*distance;
        nearClip=std::min(.1f,distance*.1f);
        farClip=std::max(1000.f,distance+radius*2);
    }
    PerspectiveCamera& Get(float width,float height){ RICE_BENCHMARK_FUNCTION(); camera.SetPerspective(45,width/std::max(1.f,height),nearClip,farClip);camera.SetPosition(position);camera.LookAtObj(position+Forward());return camera;}
};
}
