#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "Buffer.h"

#include <glad/glad.h>

namespace Rice
{
    VertexBuffer::VertexBuffer(const float* vertices, uint32_t size)
    {
        RICE_BENCHMARK_FUNCTION();
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
    }

    VertexBuffer::~VertexBuffer()
    {
        RICE_BENCHMARK_FUNCTION();
        glDeleteBuffers(1, &m_RendererID);
    }

    void VertexBuffer::Bind() const
    {
        RICE_BENCHMARK_FUNCTION();
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    }

    void VertexBuffer::Unbind() const
    {
        RICE_BENCHMARK_FUNCTION();
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void VertexBuffer::SetData(const void* data, uint32_t size)
    {
        RICE_BENCHMARK_FUNCTION();
        if (!size) return;
        m_StreamCapacity = std::max(m_StreamCapacity, size);
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        // Orphan the previous storage so uploads need not wait for its draws.
        glBufferData(GL_ARRAY_BUFFER, m_StreamCapacity, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
    }

    IndexBuffer::IndexBuffer(const uint32_t* indices, uint32_t count)
        : m_Count(count)
    {
        RICE_BENCHMARK_FUNCTION();
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
    }

    IndexBuffer::~IndexBuffer()
    {
        RICE_BENCHMARK_FUNCTION();
        glDeleteBuffers(1, &m_RendererID);
    }

    void IndexBuffer::Bind() const
    {
        RICE_BENCHMARK_FUNCTION();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
    }

    void IndexBuffer::Unbind() const
    {
        RICE_BENCHMARK_FUNCTION();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    uint32_t IndexBuffer::GetCount() const
    {
        RICE_BENCHMARK_FUNCTION();
        return m_Count;
    }
}
