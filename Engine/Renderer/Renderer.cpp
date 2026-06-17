#include "Rice/PreCompiledHeaders.h"
#include "Renderer.h"

#include "RenderCommand.h"
#include "VertexArray.h"

namespace Rice
{
    void Renderer::BeginScene()
    {
    }

    void Renderer::EndScene()
    {
    }

    void Renderer::Submit(const std::shared_ptr<VertexArray>& vertexArray)
    {
        RenderCommand::DrawIndexed(vertexArray);
    }
}
