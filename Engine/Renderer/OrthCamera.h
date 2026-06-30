#pragma once

#include "Camera.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Rice
{
    class RICE_API OrthCamera : public Camera
    {
    private:
        Matrix4 m_ProjectionMatrix = Matrix4(1.0f);
        Matrix4 m_ViewMatrix = Matrix4(1.0f);
        Matrix4 m_VPMatrix = Matrix4(1.0f);

        Vector3 m_Position = Vector3(0.0f);
        Quaternion m_Rotation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
        Vector3 m_Scale = Vector3(1.0f);

    private:
        void ResetView(); //recalculates view matrix.

    public:
        OrthCamera(float left, float right, float bottom, float top);

        void SetPosition(const Vector3& position) override;
        const Vector3& GetPosition() const override { return m_Position; }

        void SetRotationZ(float degree) override;
        void SetRotation3D(const Vector3& axis, float degree) override;
        void AddRotation3D(const Vector3& axis, float degree) override;

        const Quaternion& GetRotation() const override { return m_Rotation; }

        void SetScale(const Vector3& scale) override;
        const Vector3& GetScale() const override { return m_Scale; }

        const Matrix4& GetProjectionMatrix() const override { return m_ProjectionMatrix; }
        const Matrix4& GetViewMatrix() const override { return m_ViewMatrix; }
        const Matrix4& GetVPMatrix() const override { return m_VPMatrix; }

        void LookAtObj(const Vector3& objPos) override;
    };
}
