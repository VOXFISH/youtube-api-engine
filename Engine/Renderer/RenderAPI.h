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
        void SetClearColor(const Vector4& color);
        void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount=0, uint32_t firstIndex=0);
        void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t instanceCount);
        void DrawLinesIndexed(const Ref<VertexArray>& vertexArray);
    };
}
