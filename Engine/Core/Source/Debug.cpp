#include "Rice/PreCompiledHeaders.h"
#include "Rice/Debug.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Rice{
    Ref<spdlog::logger> Debug::s_CoreLogger;
    Ref<spdlog::logger> Debug::s_ClientLogger;

    void Debug::Init()
    {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif

        spdlog::set_pattern("%^[%T] %n: %v%$");
        //start color range[Timestamp HH:MM:SS] Logger's Name: userText, end color range

        s_CoreLogger = spdlog::stdout_color_mt("Rice");
        s_CoreLogger->set_level(spdlog::level::trace);

        s_ClientLogger = spdlog::stdout_color_mt("APP");
        s_ClientLogger->set_level(spdlog::level::trace);
    }
}
