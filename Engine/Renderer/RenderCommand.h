#pragma once

#include "Rice/Core.h"
#include "RenderAPI.h"

namespace Rice
{
    class RICE_API RenderCommand
    {
    public:
        static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
        static void SetDepthTestEnabled(bool enabled);
        static void SetLineWidth(float width);
        static void SetClearColor(const glm::vec4& color);
        static void Clear();
        static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray);
        static void DrawLinesIndexed(const std::shared_ptr<VertexArray>& vertexArray);

    private:
        static RenderAPI* s_RenderAPI;
    };
}
