#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "Texture.h"

#include "Platform/OpenGL/OpenGLTexture.h"

namespace Rice
{
    Ref<Texture2D> Texture2D::Create(const std::string& path)
    {
        RICE_BENCHMARK_FUNCTION();
        return CreateRef<OpenGLTexture2D>(path);
    }
}
