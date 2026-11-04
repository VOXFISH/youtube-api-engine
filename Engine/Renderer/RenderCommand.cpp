#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "RenderCommand.h"

namespace Rice
{
    RenderAPI* RenderCommand::s_RenderAPI = new RenderAPI();

    void RenderCommand::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        RICE_BENCHMARK_FUNCTION();
        s_RenderAPI->SetViewport(x, y, width, height);
    }

    void RenderCommand::SetClearColor(const Vector4& color)
    {
        RICE_BENCHMARK_FUNCTION();
        s_RenderAPI->SetClearColor(color);
    }

    void RenderCommand::SetDepthTestEnabled(bool enabled)
    {
        RICE_BENCHMARK_FUNCTION();
        s_RenderAPI->SetDepthTestEnabled(enabled);
    }

    void RenderCommand::SetLineWidth(float width)
    {
        RICE_BENCHMARK_FUNCTION();
        s_RenderAPI->SetLineWidth(width);
    }

    void RenderCommand::Clear()
    {
        RICE_BENCHMARK_FUNCTION();
        s_RenderAPI->Clear();
    }

    void RenderCommand::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t firstIndex)
    {
        RICE_BENCHMARK_FUNCTION();
        s_RenderAPI->DrawIndexed(vertexArray,indexCount,firstIndex);
    }

    void RenderCommand::DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t instanceCount)
    {
        RICE_BENCHMARK_FUNCTION();
        s_RenderAPI->DrawIndexedInstanced(vertexArray,instanceCount);
    }

    void RenderCommand::DrawLinesIndexed(const Ref<VertexArray>& vertexArray)
    {
        RICE_BENCHMARK_FUNCTION();
        s_RenderAPI->DrawLinesIndexed(vertexArray);
    }
}
