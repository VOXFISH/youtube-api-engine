#pragma once

#include "Rice/Core.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rice
{
    class RICE_API Camera
    {
    public:
        virtual ~Camera() = default;

        virtual void SetPosition(const Vector3& position) = 0;
        virtual const Vector3& GetPosition() const = 0;

        virtual void SetRotationZ(float degree) = 0;
        virtual void SetRotation3D(const Vector3& axis, float degree) = 0;
        virtual void AddRotation3D(const Vector3& axis, float degree) = 0;
        virtual const Quaternion& GetRotation() const = 0;

        virtual void SetScale(const Vector3& scale) = 0;
        virtual const Vector3& GetScale() const = 0;

        virtual const Matrix4& GetProjectionMatrix() const = 0;
        virtual const Matrix4& GetViewMatrix() const = 0;
        virtual const Matrix4& GetVPMatrix() const = 0;

        virtual void LookAtObj(const Vector3& objPos) = 0;
    };
}
