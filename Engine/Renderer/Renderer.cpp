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
        m_SceneData->ViewProjectionMatrix = camera.GetVPMatrix();
    }

    void Renderer::EndScene()
    {
    }

    void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const Matrix4& transform)
    {
        shader->Bind();
        shader->SetUniformMat4("u_ViewProjection", m_SceneData->ViewProjectionMatrix);
        shader->SetUniformMat4("u_Transform", transform);

        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
    }
}
