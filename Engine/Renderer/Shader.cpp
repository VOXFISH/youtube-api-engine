#include "Rice/PreCompiledHeaders.h"
#include "Shader.h"
#include "Platform/OpenGL/OpenGLShader.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

namespace Rice
{
    Shader* Shader::Create(const std::string& vertexSource, const std::string& fragmentSource)
    {
        return new OpenGLShader(vertexSource, fragmentSource);
    }
}
