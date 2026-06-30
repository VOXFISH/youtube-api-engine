#pragma once

#include <memory>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#ifdef _WIN32
    #ifdef RICE_STATIC
        #define RICE_API
    #elif defined(RICE_BUILD_DLL)
        #define RICE_API __declspec(dllexport)
    #else
        #define RICE_API __declspec(dllimport)
    #endif
#else
    #error Other environments are not supported yet.
#endif

#ifdef RICE_ENABLE_ASSERTS
    #define RICE_ASSERT(x, ...) { if (!(x)) { RICE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
    #define RICE_CORE_ASSERT(x, ...) { if (!(x)) { RICE_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
    #define RICE_ASSERT(x, ...)
    #define RICE_CORE_ASSERT(x, ...)
#endif

#define BIT(x) (1 << (x))

namespace Rice
{
    template <typename T>
    using Scope = std::unique_ptr<T>;

    template <typename T, typename... Args>
    constexpr Scope<T> CreateScope(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    template <typename T>
    using Ref = std::shared_ptr<T>;

    template <typename T, typename... Args>
    constexpr Ref<T> CreateRef(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    using Vector2 = glm::vec2;
    using Vector3 = glm::vec3;
    using Vector4 = glm::vec4;

    using IntVector2 = glm::ivec2;
    using IntVector3 = glm::ivec3;
    using IntVector4 = glm::ivec4;

    using Matrix3 = glm::mat3;
    using Matrix4 = glm::mat4;
    using Quaternion = glm::quat;

    using Color = Vector4;
}
