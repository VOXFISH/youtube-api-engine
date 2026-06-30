#pragma once

#include "Renderer/Shader.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace Rice
{
    class RICE_API OpenGLShader : public Shader
    {
    public:
        OpenGLShader(const std::string& vertexSrc, const std::string& fragmentSrc);
        ~OpenGLShader() override;

        void Bind() const override;
        void Unbind() const override;
        void SetUniformInt(const std::string& name, int value) const override;
        void SetUniformInt2(const std::string& name, const IntVector2& value) const override;
        void SetUniformInt3(const std::string& name, const IntVector3& value) const override;
        void SetUniformInt4(const std::string& name, const IntVector4& value) const override;
        void SetUniformFloat(const std::string& name, float value) const override;
        void SetUniformFloat2(const std::string& name, const Vector2& value) const override;
        void SetUniformFloat3(const std::string& name, const Vector3& value) const override;
        void SetUniformFloat4(const std::string& name, const Vector4& value) const override;
        void SetUniformMat3(const std::string& name, const Matrix3& value) const override;
        void SetUniformMat4(const std::string& name, const Matrix4& value) const override;

    private:
        uint32_t m_RendererID = 0;
    };
}
