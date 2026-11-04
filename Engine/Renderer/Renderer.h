#pragma once

#include "Rice/Core.h"
#include "Camera.h"

#include <memory>

namespace Rice
{
    class VertexArray;
    class Shader;

    class RICE_API Renderer
    {
    private:
        struct SceneData
        {
            Matrix4 ViewProjectionMatrix;
        };
        static SceneData* m_SceneData;
    public:
        static void BeginScene(const Camera& camera);
        static void EndScene();
        static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const Matrix4& transform, uint32_t indexCount=0, uint32_t firstIndex=0);
        static void SubmitInstanced(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, uint32_t instanceCount);
    };
}
