#include "SandboxCube.h"

#include "Renderer/Buffer.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Renderer/VertexArray.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>

namespace Sandbox
{
    namespace
    {
        struct AtlasCell
        {
            float u0;
            float v0;
            float u1;
            float v1;
        };

        AtlasCell GetAtlasCell(int row, int column)
        {
            constexpr float columns = 4.0f;
            constexpr float rows = 3.0f;

            const float u0 = static_cast<float>(column - 1) / columns;
            const float u1 = static_cast<float>(column) / columns;
            const float v0 = 1.0f - static_cast<float>(row) / rows;
            const float v1 = 1.0f - static_cast<float>(row - 1) / rows;

            return { u0, v0, u1, v1 };
        }

        std::string ResolveTexturePath(const std::string& texturePath)
        {
            const std::filesystem::path path(texturePath);
            if (std::filesystem::exists(path))
                return path.string();

            const std::filesystem::path candidates[] = {
                std::filesystem::path("..") / path,
                std::filesystem::path("..") / ".." / path,
                std::filesystem::path("..") / ".." / ".." / path
            };

            for (const auto& candidate : candidates)
            {
                if (std::filesystem::exists(candidate))
                    return candidate.string();
            }

            return texturePath;
        }

        Rice::Quaternion BuildUpRotation(const Rice::Vector3& upDirection)
        {
            constexpr float epsilon = 0.00001f;

            const Rice::Vector3 localUp(0.0f, 1.0f, 0.0f);
            if (glm::length(upDirection) < epsilon)
                return Rice::Quaternion(1.0f, 0.0f, 0.0f, 0.0f);

            const Rice::Vector3 targetUp = glm::normalize(upDirection);
            const float alignment = glm::clamp(glm::dot(localUp, targetUp), -1.0f, 1.0f);

            if (alignment > 1.0f - epsilon)
                return Rice::Quaternion(1.0f, 0.0f, 0.0f, 0.0f);

            if (alignment < -1.0f + epsilon)
                return glm::angleAxis(glm::pi<float>(), Rice::Vector3(1.0f, 0.0f, 0.0f));

            const Rice::Vector3 axis = glm::normalize(glm::cross(localUp, targetUp));
            return glm::normalize(glm::angleAxis(std::acos(alignment), axis));
        }
    }

    SandboxCube::SandboxCube(
        const Rice::Vector3& size,
        const Rice::Vector3& position,
        const Rice::Vector3& upDirection,
        const Rice::Color& color)
        : m_Size(size), m_Position(position), m_UpDirection(upDirection), m_Color(color)
    {
        const AtlasCell top = GetAtlasCell(1, 2);
        const AtlasCell left = GetAtlasCell(2, 1);
        const AtlasCell front = GetAtlasCell(2, 2);
        const AtlasCell right = GetAtlasCell(2, 3);
        const AtlasCell back = GetAtlasCell(2, 4);
        const AtlasCell bottom = GetAtlasCell(3, 2);

        const float vertices[] = {
            -0.5f, -0.5f, -0.5f,  back.u0, back.v0,
             0.5f, -0.5f, -0.5f,  back.u1, back.v0,
             0.5f,  0.5f, -0.5f,  back.u1, back.v1,
            -0.5f,  0.5f, -0.5f,  back.u0, back.v1,

            -0.5f, -0.5f,  0.5f,  front.u0, front.v0,
             0.5f, -0.5f,  0.5f,  front.u1, front.v0,
             0.5f,  0.5f,  0.5f,  front.u1, front.v1,
            -0.5f,  0.5f,  0.5f,  front.u0, front.v1,

            -0.5f, -0.5f, -0.5f,  left.u0, left.v0,
            -0.5f, -0.5f,  0.5f,  left.u1, left.v0,
            -0.5f,  0.5f,  0.5f,  left.u1, left.v1,
            -0.5f,  0.5f, -0.5f,  left.u0, left.v1,

             0.5f, -0.5f,  0.5f,  right.u0, right.v0,
             0.5f, -0.5f, -0.5f,  right.u1, right.v0,
             0.5f,  0.5f, -0.5f,  right.u1, right.v1,
             0.5f,  0.5f,  0.5f,  right.u0, right.v1,

            -0.5f,  0.5f,  0.5f,  top.u0, top.v0,
             0.5f,  0.5f,  0.5f,  top.u1, top.v0,
             0.5f,  0.5f, -0.5f,  top.u1, top.v1,
            -0.5f,  0.5f, -0.5f,  top.u0, top.v1,

            -0.5f, -0.5f, -0.5f,  bottom.u0, bottom.v0,
             0.5f, -0.5f, -0.5f,  bottom.u1, bottom.v0,
             0.5f, -0.5f,  0.5f,  bottom.u1, bottom.v1,
            -0.5f, -0.5f,  0.5f,  bottom.u0, bottom.v1
        };

        const uint32_t indices[] = {
             0,  1,  2,  2,  3,  0,
             4,  5,  6,  6,  7,  4,
             8,  9, 10, 10, 11,  8,
            12, 13, 14, 14, 15, 12,
            16, 17, 18, 18, 19, 16,
            20, 21, 22, 22, 23, 20
        };

        m_VertexArray = Rice::CreateRef<Rice::VertexArray>();

        Rice::Ref<Rice::VertexBuffer> vertexBuffer = Rice::CreateRef<Rice::VertexBuffer>(
            vertices,
            static_cast<uint32_t>(sizeof(vertices)));
        vertexBuffer->SetLayout({
            { Rice::ShaderDataType::Float3, "a_Position" },
            { Rice::ShaderDataType::Float2, "a_TexCoord" }
        });
        m_VertexArray->AddVertexBuffer(vertexBuffer);

        Rice::Ref<Rice::IndexBuffer> indexBuffer = Rice::CreateRef<Rice::IndexBuffer>(
            indices,
            static_cast<uint32_t>(sizeof(indices) / sizeof(uint32_t)));
        m_VertexArray->SetIndexBuffer(indexBuffer);

        const char* vertexSource = R"(
            #version 330 core

            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec2 a_TexCoord;

            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;

            out vec2 v_TexCoord;

            void main()
            {
                v_TexCoord = a_TexCoord;
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
            }
        )";

        const char* fragmentSource = R"(
            #version 330 core

            layout(location = 0) out vec4 o_Color;

            uniform vec4 u_Color;
            uniform sampler2D u_Texture;
            uniform int u_UseTexture;

            in vec2 v_TexCoord;

            void main()
            {
                vec4 textureColor = texture(u_Texture, v_TexCoord);
                o_Color = u_UseTexture == 1 ? textureColor * u_Color : u_Color;
            }
        )";

        m_Shader = Rice::Ref<Rice::Shader>(Rice::Shader::Create(vertexSource, fragmentSource));
    }

    void SandboxCube::ApplyGrassTexture(const std::string& texturePath)
    {
        m_Texture = Rice::Texture2D::Create(ResolveTexturePath(texturePath));
    }

    void SandboxCube::Render() const
    {
        m_Shader->Bind();
        m_Shader->SetUniformFloat4("u_Color", m_Color);
        m_Shader->SetUniformInt("u_UseTexture", m_Texture ? 1 : 0);
        m_Shader->SetUniformInt("u_Texture", 0);

        if (m_Texture)
            m_Texture->Bind(0);

        Rice::Renderer::Submit(m_Shader, m_VertexArray, BuildTransform());
    }

    Rice::Matrix4 SandboxCube::BuildTransform() const
    {
        const Rice::Matrix4 translation = glm::translate(Rice::Matrix4(1.0f), m_Position);
        const Rice::Matrix4 rotation = glm::mat4_cast(BuildUpRotation(m_UpDirection));
        const Rice::Matrix4 scale = glm::scale(Rice::Matrix4(1.0f), m_Size);

        return translation * rotation * scale;
    }
}
