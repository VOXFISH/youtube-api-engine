#pragma once

#include <glm/glm.hpp>
#include <memory>

namespace Rice
{
    class IndexBuffer;
    class Shader;
    class VertexArray;
    class VertexBuffer;
}

class RainbowCube
{
public:
    void Init();
    void Render(float viewportWidth, float viewportHeight);
    void Shutdown();

private:
    glm::vec3 m_Center = glm::vec3(0.0f);

    std::shared_ptr<Rice::Shader> m_Shader;
    std::shared_ptr<Rice::VertexArray> m_VertexArray;
    std::shared_ptr<Rice::VertexBuffer> m_VertexBuffer;
    std::shared_ptr<Rice::IndexBuffer> m_IndexBuffer;
};
