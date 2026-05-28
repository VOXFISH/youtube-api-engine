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