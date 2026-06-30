#pragma once

#include "Rice/Core.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace Rice
{
    class RICE_API Shader
    {
    public:
        virtual ~Shader() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;
        virtual void SetUniformInt(const std::string& name, int value) const = 0;
        virtual void SetUniformInt2(const std::string& name, const IntVector2& value) const = 0;
        virtual void SetUniformInt3(const std::string& name, const IntVector3& value) const = 0;
        virtual void SetUniformInt4(const std::string& name, const IntVector4& value) const = 0;
        virtual void SetUniformFloat(const std::string& name, float value) const = 0;
        virtual void SetUniformFloat2(const std::string& name, const Vector2& value) const = 0;
        virtual void SetUniformFloat3(const std::string& name, const Vector3& value) const = 0;
        virtual void SetUniformFloat4(const std::string& name, const Vector4& value) const = 0;
        virtual void SetUniformMat3(const std::string& name, const Matrix3& value) const = 0;
        virtual void SetUniformMat4(const std::string& name, const Matrix4& value) const = 0;

        static Shader* Create(const std::string& vertexSource, const std::string& fragmentSource);
    };
}
