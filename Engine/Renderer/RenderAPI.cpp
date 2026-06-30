#include "Rice/PreCompiledHeaders.h"
#include "RenderAPI.h"

#include "VertexArray.h"

#include <glad/glad.h>

namespace Rice
{
    void RenderAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        glViewport(
            static_cast<GLint>(x),
            static_cast<GLint>(y),
            static_cast<GLsizei>(width),
            static_cast<GLsizei>(height));
    }

    void RenderAPI::Clear()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void RenderAPI::SetDepthTestEnabled(bool enabled)
    {
        enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    }

    void RenderAPI::SetLineWidth(float width)
    {
        glLineWidth(width);
    }

    void RenderAPI::SetClearColor(const Vector4& color)
    {
        glClearColor(color.r, color.g, color.b, color.a);
    }

    void RenderAPI::DrawIndexed(const Ref<VertexArray>& vertexArray)
    {
        vertexArray->Bind();
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(vertexArray->GetIndexBuffer()->GetCount()),
            GL_UNSIGNED_INT,
            nullptr);
    }

    void RenderAPI::DrawLinesIndexed(const Ref<VertexArray>& vertexArray)
    {
        vertexArray->Bind();
        glDrawElements(
            GL_LINES,
            static_cast<GLsizei>(vertexArray->GetIndexBuffer()->GetCount()),
            GL_UNSIGNED_INT,
            nullptr);
    }
}
