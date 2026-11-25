#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "VertexArray.h"

#include <glad/glad.h>

namespace Rice
{
    static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type)
    {
        RICE_BENCHMARK_FUNCTION();
        switch (type)
        {
            case ShaderDataType::Float:
            case ShaderDataType::Float2:
            case ShaderDataType::Float3:
            case ShaderDataType::Float4:
            case ShaderDataType::Mat3:
            case ShaderDataType::Mat4:
                return GL_FLOAT;
            case ShaderDataType::Int:
            case ShaderDataType::Int2:
            case ShaderDataType::Int3:
            case ShaderDataType::Int4:
                return GL_INT;
            case ShaderDataType::Bool:
                return GL_BOOL;
            case ShaderDataType::None:
                break;
        }

        RICE_CORE_WARNING("Unknown shader data type.");
        return 0;
    }

    VertexArray::VertexArray()
    {
        RICE_BENCHMARK_FUNCTION();
        glGenVertexArrays(1, &m_RendererID);
    }

    VertexArray::~VertexArray()
    {
        RICE_BENCHMARK_FUNCTION();
        glDeleteVertexArrays(1, &m_RendererID);
    }

    void VertexArray::Bind() const
    {
        RICE_BENCHMARK_FUNCTION();
        glBindVertexArray(m_RendererID);
    }

    void VertexArray::Unbind() const
    {
        RICE_BENCHMARK_FUNCTION();
        glBindVertexArray(0);
    }

    void VertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer, uint32_t divisor)
    {
        RICE_BENCHMARK_FUNCTION();
        glBindVertexArray(m_RendererID);
        vertexBuffer->Bind();

        const auto& layout = vertexBuffer->GetLayout();
        for (const auto& element : layout)
        {
            const uint32_t columns = element.type == ShaderDataType::Mat4 ? 4 : element.type == ShaderDataType::Mat3 ? 3 : 1;
            const GLint components = static_cast<GLint>(columns == 1 ? element.GetComponentCount() : columns);
            for (uint32_t column = 0; column < columns; ++column)
            {
                const uint32_t index = m_VertexBufferIndex++;
                glEnableVertexAttribArray(index);
                const auto baseType = ShaderDataTypeToOpenGLBaseType(element.type);
                const auto stride = static_cast<GLsizei>(layout.GetStride());
                const auto offset = reinterpret_cast<const void*>(static_cast<uintptr_t>(element.offset + column * columns * sizeof(float)));
                if (baseType == GL_INT && !element.isNormalized)
                    glVertexAttribIPointer(index, components, baseType, stride, offset);
                else
                    glVertexAttribPointer(index, components, baseType, element.isNormalized ? GL_TRUE : GL_FALSE, stride, offset);
                glVertexAttribDivisor(index, divisor);
            }
        }

        m_VertexBuffers.push_back(vertexBuffer);
    }

    void VertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
    {
        RICE_BENCHMARK_FUNCTION();
        glBindVertexArray(m_RendererID);
        indexBuffer->Bind();
        m_IndexBuffer = indexBuffer;
    }
}
