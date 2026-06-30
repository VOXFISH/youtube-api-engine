#include "Rice/PreCompiledHeaders.h"
#include "Texture.h"

#include "Platform/OpenGL/OpenGLTexture.h"

namespace Rice
{
    Ref<Texture2D> Texture2D::Create(const std::string& path)
    {
        return CreateRef<OpenGLTexture2D>(path);
    }
}
