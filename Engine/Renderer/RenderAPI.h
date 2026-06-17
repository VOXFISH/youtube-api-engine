#pragma once

#include <glm/glm.hpp>
#include <memory>

namespace Rice
{
    class VertexArray;

    class RenderAPI
    {
    public:
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
        void SetDepthTestEnabled(bool enabled);
        void SetLineWidth(float width);
        void Clear();
        void SetClearColor(const glm::vec4& color);
        void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray);
        void DrawLinesIndexed(const std::shared_ptr<VertexArray>& vertexArray);
    };
}
