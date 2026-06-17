#pragma once

#ifdef _WIN32
    #ifdef RICE_BUILD_DLL
        #define RICE_API __declspec(dllexport)
    #else
        #define RICE_API __declspec(dllimport)
    #endif
#else
    #error other environments to be supported...
#endif

#ifdef RICE_ENABLE_ASSERTS //디버거 breakpoint를 설정
    #define RICE_ASSERT(x, ...) {if(!(x)) { RICE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak();}}
    #define RICE_CORE_ASSERT(x, ...) {if(!(x)){ RICE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak();}}
#else
    #define RICE_ASSERT(x, ...)
    #define RICE_CORE_ASSERT(x, ...)
#endif

#define BIT(x) (1 << (x))