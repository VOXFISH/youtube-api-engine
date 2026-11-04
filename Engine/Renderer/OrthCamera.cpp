#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "OrthCamera.h"

namespace Rice
{
    OrthCamera::OrthCamera(float l, float r, float b, float t)
        : m_ProjectionMatrix(glm::ortho(l, r, b, t, -1.0f, 1.0f))
    {
        RICE_BENCHMARK_FUNCTION();
        ResetView();
    }

    void OrthCamera::ResetView()
    {
        RICE_BENCHMARK_FUNCTION();
        Matrix4 translation = glm::translate(Matrix4(1.0f), m_Position);
        Matrix4 rotation = glm::mat4_cast(m_Rotation);
        Matrix4 scale = glm::scale(Matrix4(1.0f), m_Scale);

        Matrix4 transform = translation * rotation * scale;

        m_ViewMatrix = glm::inverse(transform);
        m_VPMatrix = m_ProjectionMatrix * m_ViewMatrix;
    }

    void OrthCamera::SetPosition(const Vector3& position)
    {
        RICE_BENCHMARK_FUNCTION();
        m_Position = position;
        ResetView();
    }

    void OrthCamera::SetRotationZ(float degree)
    {
        RICE_BENCHMARK_FUNCTION();
        m_Rotation = glm::angleAxis(
            glm::radians(degree),
            Vector3(0.0f, 0.0f, 1.0f)
        );

        m_Rotation = glm::normalize(m_Rotation);
        ResetView();
    }

    void OrthCamera::SetRotation3D(const Vector3& axis, float degree)
    {
        RICE_BENCHMARK_FUNCTION();
        if (glm::length(axis) == 0.0f)
            return;

        Vector3 normalizedAxis = glm::normalize(axis);

        m_Rotation = glm::angleAxis(
            glm::radians(degree),
            normalizedAxis
        );

        m_Rotation = glm::normalize(m_Rotation);
        ResetView();
    }

    void OrthCamera::AddRotation3D(const Vector3& axis, float degree)
    {
        RICE_BENCHMARK_FUNCTION();
        if (glm::length(axis) == 0.0f)
            return;

        Vector3 normalizedAxis = glm::normalize(axis);

        Quaternion delta = glm::angleAxis(
            glm::radians(degree),
            normalizedAxis
        );

        m_Rotation = glm::normalize(delta * m_Rotation);
        ResetView();
    }

    void OrthCamera::SetScale(const Vector3& scale)
    {
        RICE_BENCHMARK_FUNCTION();
        m_Scale = scale;
        ResetView();
    }

    void OrthCamera::LookAtObj(const Vector3& objPos)
    {
        RICE_BENCHMARK_FUNCTION();
        m_ViewMatrix = glm::lookAt(
            m_Position,
            objPos,
            Vector3(0.0f, 1.0f, 0.0f)
        );

        m_VPMatrix = m_ProjectionMatrix * m_ViewMatrix;
    }
}
