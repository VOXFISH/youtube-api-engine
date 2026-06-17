#include "Rice/PreCompiledHeaders.h"
#include "Shader.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

namespace Rice
{
    Shader::Shader(const std::string& vertexSource, const std::string& fragmentSource)
    {
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        const GLchar* source = vertexSource.c_str();
        glShaderSource(vertexShader, 1, &source, nullptr);
        glCompileShader(vertexShader);

        GLint isCompiled = GL_FALSE;
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &isCompiled);
        if (isCompiled == GL_FALSE)
        {
            GLint maxLength = 0;
            glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength);

            std::vector<GLchar> infoLog(static_cast<size_t>(maxLength > 1 ? maxLength : 1));
            glGetShaderInfoLog(vertexShader, maxLength, nullptr, infoLog.data());
            glDeleteShader(vertexShader);

            RICE_CORE_WARNING("Vertex shader compilation failed.");
            RICE_CORE_INFO("{0}", infoLog.data());
            return;
        }

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        source = fragmentSource.c_str();
        glShaderSource(fragmentShader, 1, &source, nullptr);
        glCompileShader(fragmentShader);

        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &isCompiled);
        if (isCompiled == GL_FALSE)
        {
            GLint maxLength = 0;
            glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);

            std::vector<GLchar> infoLog(static_cast<size_t>(maxLength > 1 ? maxLength : 1));
            glGetShaderInfoLog(fragmentShader, maxLength, nullptr, infoLog.data());
            glDeleteShader(fragmentShader);
            glDeleteShader(vertexShader);

            RICE_CORE_WARNING("Fragment shader compilation failed.");
            RICE_CORE_INFO("{0}", infoLog.data());
            return;
        }

        m_RendererID = glCreateProgram();
        glAttachShader(m_RendererID, vertexShader);
        glAttachShader(m_RendererID, fragmentShader);
        glLinkProgram(m_RendererID);

        GLint isLinked = GL_FALSE;
        glGetProgramiv(m_RendererID, GL_LINK_STATUS, &isLinked);
        if (isLinked == GL_FALSE)
        {
            GLint maxLength = 0;
            glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &maxLength);

            std::vector<GLchar> infoLog(static_cast<size_t>(maxLength > 1 ? maxLength : 1));
            glGetProgramInfoLog(m_RendererID, maxLength, nullptr, infoLog.data());
            glDeleteProgram(m_RendererID);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            m_RendererID = 0;

            RICE_CORE_WARNING("Shader link failed.");
            RICE_CORE_INFO("{0}", infoLog.data());
            return;
        }

        glDetachShader(m_RendererID, vertexShader);
        glDetachShader(m_RendererID, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    Shader::~Shader()
    {
        glDeleteProgram(m_RendererID);
    }

    void Shader::Bind() const
    {
        glUseProgram(m_RendererID);
    }

    void Shader::Unbind() const
    {
        glUseProgram(0);
    }

    void Shader::SetMat4(const std::string& name, const glm::mat4& value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
    }
}
