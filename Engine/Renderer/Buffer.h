#pragma once

#include "Rice/Benchmark.h"
#include "Rice/Core.h"
#include "Rice/Debug.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace Rice
{
    enum class ShaderDataType
    {
        None = 0,
        Float,
        Float2,
        Float3,
        Float4,
        Mat3,
        Mat4,
        Int,
        Int2,
        Int3,
        Int4,
        Bool
    };

    static uint32_t ShaderDataTypeSize(ShaderDataType type)
    {
        RICE_BENCHMARK_FUNCTION();
        switch (type)
        {
            case ShaderDataType::Float:  return 4;
            case ShaderDataType::Float2: return 8;
            case ShaderDataType::Float3: return 12;
            case ShaderDataType::Float4: return 16;
            case ShaderDataType::Mat3:   return 36;
            case ShaderDataType::Mat4:   return 64;
            case ShaderDataType::Int:    return 4;
            case ShaderDataType::Int2:   return 8;
            case ShaderDataType::Int3:   return 12;
            case ShaderDataType::Int4:   return 16;
            case ShaderDataType::Bool:   return 1;
            case ShaderDataType::None:   break;
        }

        RICE_CORE_WARNING("Unknown shader data type.");
        return 0;
    }

    struct BufferElement
    {
        BufferElement() = default;

        BufferElement(ShaderDataType t, const std::string& n, bool normalized = false)
            : name(n), type(t), offset(0), size(ShaderDataTypeSize(t)), isNormalized(normalized)
        {
            RICE_BENCHMARK_FUNCTION();
        }

        uint32_t GetComponentCount() const
        {
            RICE_BENCHMARK_FUNCTION();
            switch (type)
            {
                case ShaderDataType::Float:  return 1;
                case ShaderDataType::Float2: return 2;
                case ShaderDataType::Float3: return 3;
                case ShaderDataType::Float4: return 4;
                case ShaderDataType::Mat3:   return 9;
                case ShaderDataType::Mat4:   return 16;
                case ShaderDataType::Int:    return 1;
                case ShaderDataType::Int2:   return 2;
                case ShaderDataType::Int3:   return 3;
                case ShaderDataType::Int4:   return 4;
                case ShaderDataType::Bool:   return 1;
                case ShaderDataType::None:   break;
            }

            RICE_CORE_WARNING("Unknown shader data type.");
            return 0;
        }

        std::string name;
        ShaderDataType type = ShaderDataType::None;
        uint32_t offset = 0;
        uint32_t size = 0;
        bool isNormalized = false;
    };

    class BufferLayout
    {
    public:
        BufferLayout() = default;

        BufferLayout(const std::initializer_list<BufferElement>& elements)
            : m_Elements(elements)
        {
            RICE_BENCHMARK_FUNCTION();
            CalculateOffsetAndStride();
        }

        uint32_t GetStride() const { RICE_BENCHMARK_FUNCTION(); return m_Stride; }
        const std::vector<BufferElement>& GetElements() const { RICE_BENCHMARK_FUNCTION(); return m_Elements; }

        std::vector<BufferElement>::iterator begin() { RICE_BENCHMARK_FUNCTION(); return m_Elements.begin(); }
        std::vector<BufferElement>::iterator end() { RICE_BENCHMARK_FUNCTION(); return m_Elements.end(); }
        std::vector<BufferElement>::const_iterator begin() const { RICE_BENCHMARK_FUNCTION(); return m_Elements.begin(); }
        std::vector<BufferElement>::const_iterator end() const { RICE_BENCHMARK_FUNCTION(); return m_Elements.end(); }

    private:
        void CalculateOffsetAndStride()
        {
            RICE_BENCHMARK_FUNCTION();
            uint32_t offset = 0;
            m_Stride = 0;
            for (auto& element : m_Elements)
            {
                element.offset = offset;
                offset += element.size;
                m_Stride += element.size;
            }
        }

        std::vector<BufferElement> m_Elements;
        uint32_t m_Stride = 0;
    };

    class RICE_API VertexBuffer
    {
    public:
        VertexBuffer(const float* vertices, uint32_t size);
        ~VertexBuffer();

        void Bind() const;
        void Unbind() const;
        // Stream changing instance/vertex data without rebuilding its VAO.
        void SetData(const void* data, uint32_t size);

        void SetLayout(const BufferLayout& layout) { RICE_BENCHMARK_FUNCTION(); m_Layout = layout; }
        const BufferLayout& GetLayout() const { RICE_BENCHMARK_FUNCTION(); return m_Layout; }

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_StreamCapacity = 0;
        BufferLayout m_Layout;
    };

    class RICE_API IndexBuffer
    {
    public:
        IndexBuffer(const uint32_t* indices, uint32_t count);
        ~IndexBuffer();

        uint32_t GetCount() const;
        void Bind() const;
        void Unbind() const;

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_Count = 0;
    };
}
