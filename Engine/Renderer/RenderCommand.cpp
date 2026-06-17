#include "Rice/PreCompiledHeaders.h"
#include "RenderCommand.h"

namespace Rice
{
    RenderAPI* RenderCommand::s_RenderAPI = new RenderAPI();

    void RenderCommand::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        s_RenderAPI->SetViewport(x, y, width, height);
    }

    void RenderCommand::SetClearColor(const glm::vec4& color)
    {
        s_RenderAPI->SetClearColor(color);
    }

    void RenderCommand::SetDepthTestEnabled(bool enabled)
    {
        s_RenderAPI->SetDepthTestEnabled(enabled);
    }

    void RenderCommand::SetLineWidth(float width)
    {
        s_RenderAPI->SetLineWidth(width);
    }

    void RenderCommand::Clear()
    {
        s_RenderAPI->Clear();
    }

    void RenderCommand::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray)
    {
        s_RenderAPI->DrawIndexed(vertexArray);
    }

    void RenderCommand::DrawLinesIndexed(const std::shared_ptr<VertexArray>& vertexArray)
    {
        s_RenderAPI->DrawLinesIndexed(vertexArray);
    }
}
