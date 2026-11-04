#pragma once

#include "Rice/Benchmark.h"
#include "Rice/Core.h"
#include "Buffer.h"

#include <memory>

namespace Rice
{
    class RICE_API VertexArray
    {
    public:
        VertexArray();
        ~VertexArray();

        void Bind() const;
        void Unbind() const;

        void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer, uint32_t divisor=0);
        void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer);

        const std::vector<Ref<VertexBuffer>>& GetVertexBuffers() const { RICE_BENCHMARK_FUNCTION(); return m_VertexBuffers; }
        const Ref<IndexBuffer>& GetIndexBuffer() const { RICE_BENCHMARK_FUNCTION(); return m_IndexBuffer; }

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_VertexBufferIndex = 0;
        std::vector<Ref<VertexBuffer>> m_VertexBuffers;
        Ref<IndexBuffer> m_IndexBuffer;
    };
}
