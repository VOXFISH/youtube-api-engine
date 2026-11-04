#pragma once
#include "Scene.h"
#include "Renderer/PerspectiveCamera.h"

namespace Rice {
// Camera looks along local -Z; its optical center is the object's pivot.
struct CameraComponent : Component {
    float fieldOfView=45,nearClip=.1f,farClip=1000;
    bool primary=true;
    PerspectiveCamera view{45,16.f/9,.1f,1000};
    bool LookAt(Vector3 target){
        auto& object=GetGameObject();auto position=object.GetWorldPivot();
        auto direction=target-position;
        if(!std::isfinite(glm::length(direction))||glm::length(direction)<1e-6f)return false;
        direction=glm::normalize(direction);
        auto up=std::abs(glm::dot(direction,Vector3(0,1,0)))>.999f?Vector3(0,0,1):Vector3(0,1,0);
        auto rotation=glm::quat_cast(glm::inverse(glm::lookAt(position,target,up)));
        auto local=Quaternion(glm::radians(object.transform.rotation));
        auto parent=object.GetWorldRotation()*glm::inverse(local);
        object.transform.rotation=glm::degrees(glm::eulerAngles(glm::normalize(glm::inverse(parent)*rotation)));
        return true;
    }
    PerspectiveCamera& View(float aspect){
        auto& object=GetGameObject();
        auto rotation=object.GetWorldRotation();
        view.SetPerspective(std::clamp(fieldOfView,1.f,179.f),std::max(aspect,.001f),std::max(nearClip,.001f),std::max(farClip,std::max(nearClip,.001f)+.001f));
        view.SetPosition(object.GetWorldPivot());
        float angle=glm::angle(rotation);auto axis=glm::axis(rotation);
        view.SetRotation3D(glm::length(axis)>1e-6f?axis:Vector3(0,1,0),glm::degrees(angle));
        return view;
    }
    static Camera& GameView(Scene& scene,float aspect,Camera& fallback){
        for(auto& object:scene.GetObjects())if(object->IsActive())
            if(auto* camera=object->GetComponent<CameraComponent>();camera&&camera->enabled&&camera->primary)return camera->View(aspect);
        return fallback;
    }
};
struct CameraFollower : Component {
    EntityID camera=0;
    Transform transform;
    bool showInEditor=true;
    bool evaluating=false;
    // Align the pivot-relative follower frame with the camera frame.
    bool Follow(){
        auto& object=GetGameObject();auto* target=object.GetScene().Find(camera);
        if(!enabled||!object.IsActive()||!target||!target->IsActive()||target->DependsOn(&object)||object.ParentAnchor())return false;
        auto* component=target->GetComponent<CameraComponent>();
        if(!component||!component->enabled)return false;
        if(evaluating)return false;
        evaluating=true;struct Reset {bool& flag;~Reset(){flag=false;}} reset{evaluating};
        if(auto* follower=target->GetComponent<CameraFollower>();follower&&follower->enabled&&!follower->Follow())return false;
        auto offset=transform.GetMatrix();auto parent=object.GetParentMatrix();
        if(std::abs(glm::determinant(offset))<1e-8f||std::abs(glm::determinant(parent))<1e-8f)return false;
        auto frame=glm::translate(Matrix4(1),target->GetWorldPivot())*glm::mat4_cast(target->GetWorldRotation());
        object.SetWorldMatrix(frame*glm::inverse(offset)*glm::translate(Matrix4(1),-object.GetLocalPivot()));
        return true;
    }
    void LateUpdate(float)override{Follow();}
};
}
