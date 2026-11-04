#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "Renderer.h"

#include "RenderCommand.h"
#include "Shader.h"
#include "VertexArray.h"

namespace Rice
{
    Renderer::SceneData* Renderer::m_SceneData = new Renderer::SceneData();

    void Renderer::BeginScene(const Camera& camera)
    {
        RICE_BENCHMARK_FUNCTION();
        m_SceneData->ViewProjectionMatrix = camera.GetVPMatrix();
    }

    void Renderer::EndScene()
    {
        RICE_BENCHMARK_FUNCTION();
    }

    void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const Matrix4& transform, uint32_t indexCount, uint32_t firstIndex)
    {
        RICE_BENCHMARK_FUNCTION();
        shader->Bind();
        shader->SetUniformMat4("u_ViewProjection", m_SceneData->ViewProjectionMatrix);
        shader->SetUniformMat4("u_Transform", transform);

        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray,indexCount,firstIndex);
    }

    void Renderer::SubmitInstanced(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, uint32_t instanceCount)
    {
        RICE_BENCHMARK_FUNCTION();
        shader->Bind();
        shader->SetUniformMat4("u_ViewProjection", m_SceneData->ViewProjectionMatrix);
        RenderCommand::DrawIndexedInstanced(vertexArray, instanceCount);
    }
}
