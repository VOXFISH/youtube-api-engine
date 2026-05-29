#pragma once

#include "Core.h"
#include "spdlog/spdlog.h"

namespace Rice{

    class RICE_API Debug
    {
        public:
            static void Init();
            
            inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
            inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
        private:
            static std::shared_ptr<spdlog::logger> s_CoreLogger;
            static std::shared_ptr<spdlog::logger> s_ClientLogger;

    };
}

#define RICE_CORE_FATAL(...) ::Rice::Debug::GetCoreLogger()->critical(__VA_ARGS__)
#define RICE_CORE_ERROR(...) ::Rice::Debug::GetCoreLogger()->error(__VA_ARGS__)
#define RICE_CORE_WARNING(...) ::Rice::Debug::GetCoreLogger()->warn(__VA_ARGS__)
#define RICE_ERROR(...) ::Rice::Debug::GetClientLogger()->error(__VA_ARGS__)
#define RICE_LOG(...) ::Rice::Debug::GetClientLogger()->trace(__VA_ARGS__)
