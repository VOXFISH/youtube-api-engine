#include "RainbowCube.h"

#include "Renderer/Buffer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Shader.h"
#include "Renderer/VertexArray.h"
#include "Input/Input.h"
#include "Event/MouseCode.h"

#include <GLFW/glfw3.h>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

void RainbowCube::Init()
{
    const char* vertexShaderSource = R"(
        #version 330 core

        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec3 a_Color;

        uniform mat4 u_MVP;

        out vec3 v_Color;

        void main()
        {
            v_Color = a_Color;
            gl_Position = u_MVP * vec4(a_Position, 1.0);
        }
    )";

    const char* fragmentShaderSource = R"(
        #version 330 core

        in vec3 v_Color;
        out vec4 o_Color;

        void main()
        {
            o_Color = vec4(v_Color, 1.0);
        }
    )";

    // Vertices 1, 2, and 4 do not share cube edges, so they are pairwise non-adjacent.
    const std::array<float, 48> vertices = {
        -0.5f, -0.5f, -0.5f, 0.45f, 0.10f, 0.95f, // 0
         0.5f, -0.5f, -0.5f, 1.00f, 0.00f, 0.00f, // 1: R
        -0.5f,  0.5f, -0.5f, 0.00f, 1.00f, 0.00f, // 2: G
         0.5f,  0.5f, -0.5f, 1.00f, 0.95f, 0.00f, // 3
        -0.5f, -0.5f,  0.5f, 0.00f, 0.00f, 1.00f, // 4: B
         0.5f, -0.5f,  0.5f, 1.00f, 0.20f, 0.90f, // 5
        -0.5f,  0.5f,  0.5f, 0.00f, 0.95f, 0.95f, // 6
         0.5f,  0.5f,  0.5f, 1.00f, 1.00f, 1.00f  // 7
    };

    const std::array<uint32_t, 36> indices = {
        0, 1, 2, 2, 1, 3, // back
        4, 6, 5, 5, 6, 7, // front
        0, 2, 4, 4, 2, 6, // left
        1, 5, 3, 3, 5, 7, // right
        0, 4, 1, 1, 4, 5, // bottom
        2, 3, 6, 6, 3, 7  // top
    };

    m_Shader = std::make_shared<Rice::Shader>(vertexShaderSource, fragmentShaderSource);

    m_VertexArray = std::make_shared<Rice::VertexArray>();
    m_VertexBuffer = std::make_shared<Rice::VertexBuffer>(
        vertices.data(),
        static_cast<uint32_t>(vertices.size() * sizeof(float)));
    m_VertexBuffer->SetLayout({
        { Rice::ShaderDataType::Float3, "a_Position" },
        { Rice::ShaderDataType::Float3, "a_Color" }
    });
    m_IndexBuffer = std::make_shared<Rice::IndexBuffer>(
        indices.data(),
        static_cast<uint32_t>(indices.size()));

    m_VertexArray->AddVertexBuffer(m_VertexBuffer);
    m_VertexArray->SetIndexBuffer(m_IndexBuffer);

    Rice::RenderCommand::SetDepthTestEnabled(true);
}

void RainbowCube::Render(float viewportWidth, float viewportHeight)
{
    if (!m_Shader)
    {
        return;
    }

    const float width = viewportWidth <= 0.0f ? 1.0f : viewportWidth;
    const float height = viewportHeight <= 0.0f ? 1.0f : viewportHeight;
    const float aspectRatio = width / height;
    const float time = static_cast<float>(glfwGetTime());

    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.8f));
    const glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);

    if (Rice::Input::IsMouseButtonPressed(Rice::Mouse::ButtonLeft))
    {
        RICE_LOG("MouseButtonPressed!");
        const auto [mouseX, mouseY] = Rice::Input::GetMousePosition();
        const float normalizedX = (2.0f * mouseX) / width - 1.0f;
        const float normalizedY = 1.0f - (2.0f * mouseY) / height;
        const glm::mat4 inverseViewProjection = glm::inverse(projection * view);

        glm::vec4 nearPoint = inverseViewProjection * glm::vec4(normalizedX, normalizedY, -1.0f, 1.0f);
        glm::vec4 farPoint = inverseViewProjection * glm::vec4(normalizedX, normalizedY, 1.0f, 1.0f);
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        const glm::vec3 rayStart = glm::vec3(nearPoint);
        const glm::vec3 rayDirection = glm::normalize(glm::vec3(farPoint - nearPoint));
        if (std::abs(rayDirection.z) > 0.0001f)
        {
            const float t = -rayStart.z / rayDirection.z;
            m_Center = rayStart + rayDirection * t;
        }
    }

    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), m_Center) *
        glm::rotate(
            glm::mat4(1.0f),
            time * 0.8f,
            glm::normalize(glm::vec3(0.4f, 1.0f, 0.25f))) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));

    m_Shader->Bind();
    m_Shader->SetMat4("u_MVP", projection * view * model);
    Rice::RenderCommand::DrawIndexed(m_VertexArray);
}

void RainbowCube::Shutdown()
{
    m_IndexBuffer.reset();
    m_VertexBuffer.reset();
    m_VertexArray.reset();
    m_Shader.reset();
}
