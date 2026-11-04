#pragma once
#include "Rice/Benchmark.h"
#include "Rice/Core.h"

#include <string>

namespace Rice
{
    enum class CubemapLayout { None, VerticalCross, HorizontalCross, HorizontalStrip, VerticalStrip };
    inline CubemapLayout DetectCubemapLayout(uint32_t width, uint32_t height) {
        RICE_BENCHMARK_FUNCTION();
        if(!width || !height)return CubemapLayout::None;
        if(width%3==0 && height%4==0 && width/3==height/4)return CubemapLayout::VerticalCross;
        if(width%4==0 && height%3==0 && width/4==height/3)return CubemapLayout::HorizontalCross;
        if(width%6==0 && width/6==height)return CubemapLayout::HorizontalStrip;
        if(height%6==0 && height/6==width)return CubemapLayout::VerticalStrip;
        return CubemapLayout::None;
    }
    inline const char* CubemapLayoutName(CubemapLayout layout) {
        RICE_BENCHMARK_FUNCTION();
        switch(layout){
            case CubemapLayout::VerticalCross:return "Vertical cross (3 x 4)";
            case CubemapLayout::HorizontalCross:return "Horizontal cross (4 x 3)";
            case CubemapLayout::HorizontalStrip:return "Horizontal strip (6 x 1)";
            case CubemapLayout::VerticalStrip:return "Vertical strip (1 x 6)";
            default:return "2D image";
        }
    }
    class RICE_API Texture
    {
    private:
    public:
        virtual ~Texture() = default;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;

        virtual void Bind(uint32_t slot) const = 0;
    };

    class RICE_API Texture2D : public Texture
    {
    public:
        static Ref<Texture2D> Create(const std::string& path);
    };
}
