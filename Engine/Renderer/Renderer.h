#pragma once

#include "Rice/Core.h"

#include <memory>

namespace Rice
{
    class VertexArray;

    class RICE_API Renderer
    {
    public:
        static void BeginScene();
        static void EndScene();
        static void Submit(const std::shared_ptr<VertexArray>& vertexArray);
    };
}
