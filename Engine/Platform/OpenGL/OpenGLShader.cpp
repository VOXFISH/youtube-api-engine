#include "Rice/PreCompiledHeaders.h"
#include "OpenGLShader.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

namespace Rice
{
    OpenGLShader::OpenGLShader(const std::string& vertexSource, const std::string& fragmentSource)
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

    OpenGLShader::~OpenGLShader()
    {
        glDeleteProgram(m_RendererID);
    }

    void OpenGLShader::Bind() const
    {
        glUseProgram(m_RendererID);
    }

    void OpenGLShader::Unbind() const
    {
        glUseProgram(0);
    }

    void OpenGLShader::SetUniformInt(const std::string& name, int value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform1i(location, value);
    }

    void OpenGLShader::SetUniformInt2(const std::string& name, const IntVector2& value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform2i(location, value.x, value.y);
    }

    void OpenGLShader::SetUniformInt3(const std::string& name, const IntVector3& value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform3i(location, value.x, value.y, value.z);
    }

    void OpenGLShader::SetUniformInt4(const std::string& name, const IntVector4& value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform4i(location, value.x, value.y, value.z, value.w);
    }

    void OpenGLShader::SetUniformFloat(const std::string& name, float value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform1f(location, value);
    }

    void OpenGLShader::SetUniformFloat2(const std::string& name, const Vector2& value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform2f(location, value.x, value.y);
    }

    void OpenGLShader::SetUniformFloat3(const std::string& name, const Vector3& value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform3f(location, value.x, value.y, value.z);
    }

    void OpenGLShader::SetUniformFloat4(const std::string& name, const Vector4& value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniform4f(location, value.x, value.y, value.z, value.w);
    }

    void OpenGLShader::SetUniformMat3(const std::string& name, const Matrix3& value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
    }

    void OpenGLShader::SetUniformMat4(const std::string& name, const Matrix4& value) const
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
    }
}
