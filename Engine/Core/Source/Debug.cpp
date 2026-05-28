#include "Debug.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace Rice{
    static std::shared_ptr<spdlog::logger> Debug::s_CoreLogger;
    static std::shared_ptr<spdlog::logger> Debug::s_ClientLogger;

    void Debug::Init()
    {
        spdlog::set_pattern("%^[%T] %n: %v%$");
        //start color range[Timestamp HH:MM:SS] Logger's Name: userText, end color range

        s_CoreLogger = spdlog::stdout_color_mt("Rice");
        s_CoreLogger->set_level(spdlog::level::trace);

        s_ClientLogger = spdlog::stdout_color_mt("APP");
        s_ClientLogger->set_level(spdlog::level::trace);
    }
}