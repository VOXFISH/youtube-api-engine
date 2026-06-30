#pragma once

#include "Rice/Core.h"

#include <string>

namespace Rice
{
    class Shader;
    class Texture2D;
    class VertexArray;
}

namespace Sandbox
{
    class SandboxCube
    {
    public:
        SandboxCube(
            const Rice::Vector3& size,
            const Rice::Vector3& position,
            const Rice::Vector3& upDirection,
            const Rice::Color& color = Rice::Color(0.2f, 0.7f, 1.0f, 1.0f));

        void SetSize(const Rice::Vector3& size) { m_Size = size; }
        void SetPosition(const Rice::Vector3& position) { m_Position = position; }
        void SetUpDirection(const Rice::Vector3& upDirection) { m_UpDirection = upDirection; }
        void SetColor(const Rice::Color& color) { m_Color = color; }

        const Rice::Vector3& GetSize() const { return m_Size; }
        const Rice::Vector3& GetPosition() const { return m_Position; }
        const Rice::Vector3& GetUpDirection() const { return m_UpDirection; }
        const Rice::Color& GetColor() const { return m_Color; }

        void ApplyGrassTexture(const std::string& texturePath = "Sandbox/Assets/grass.png");
        void Render() const;

    private:
        Rice::Matrix4 BuildTransform() const;

    private:
        Rice::Vector3 m_Size;
        Rice::Vector3 m_Position;
        Rice::Vector3 m_UpDirection;
        Rice::Color m_Color;

        Rice::Ref<Rice::VertexArray> m_VertexArray;
        Rice::Ref<Rice::Shader> m_Shader;
        Rice::Ref<Rice::Texture2D> m_Texture;
    };
}
