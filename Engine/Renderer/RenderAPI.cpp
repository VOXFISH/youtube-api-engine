#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "RenderAPI.h"

#include "VertexArray.h"

#include <glad/glad.h>

namespace Rice
{
    void RenderAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        RICE_BENCHMARK_FUNCTION();
        glViewport(
            static_cast<GLint>(x),
            static_cast<GLint>(y),
            static_cast<GLsizei>(width),
            static_cast<GLsizei>(height));
    }

    void RenderAPI::Clear()
    {
        RICE_BENCHMARK_FUNCTION();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void RenderAPI::SetDepthTestEnabled(bool enabled)
    {
        RICE_BENCHMARK_FUNCTION();
        enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    }

    void RenderAPI::SetLineWidth(float width)
    {
        RICE_BENCHMARK_FUNCTION();
        glLineWidth(width);
    }

    void RenderAPI::SetClearColor(const Vector4& color)
    {
        RICE_BENCHMARK_FUNCTION();
        glClearColor(color.r, color.g, color.b, color.a);
    }

    void RenderAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t firstIndex)
    {
        RICE_BENCHMARK_FUNCTION();
        vertexArray->Bind();
        auto available=vertexArray->GetIndexBuffer()->GetCount();
        if(firstIndex>=available)return;
        auto count=indexCount?std::min(indexCount,available-firstIndex):available-firstIndex;
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(count),
            GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(static_cast<uintptr_t>(firstIndex)*sizeof(uint32_t)));
    }

    void RenderAPI::DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t instanceCount)
    {
        RICE_BENCHMARK_FUNCTION();
        if (!instanceCount) return;
        vertexArray->Bind();
        glDrawElementsInstanced(GL_TRIANGLES,
            static_cast<GLsizei>(vertexArray->GetIndexBuffer()->GetCount()),
            GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(instanceCount));
    }

    void RenderAPI::DrawLinesIndexed(const Ref<VertexArray>& vertexArray)
    {
        RICE_BENCHMARK_FUNCTION();
        vertexArray->Bind();
        glDrawElements(
            GL_LINES,
            static_cast<GLsizei>(vertexArray->GetIndexBuffer()->GetCount()),
            GL_UNSIGNED_INT,
            nullptr);
    }
}
