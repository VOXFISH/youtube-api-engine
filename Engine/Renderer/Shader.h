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
        Shader(const std::string& vertexSrc, const std::string& fragmentSrc);
        ~Shader();

        void Bind() const;
        void Unbind() const;
        void SetMat4(const std::string& name, const glm::mat4& value) const;

    private:
        uint32_t m_RendererID = 0;
    };
}
