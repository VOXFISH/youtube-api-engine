#pragma once

#include "Rice/Benchmark.h"
#include "Core.h"
#include "spdlog/spdlog.h"
#include <vector>
#include <string>
#include <cstdint>

namespace Rice{

    struct LogEntry { uint64_t sequence; spdlog::level::level_enum level; std::string text; };

    class RICE_API Debug
    {
        public:
            static void Init();
            static std::vector<LogEntry> GetMessages();
            static void ClearMessages();
            static constexpr size_t MessageCapacity = 2000;
            
            inline static Ref<spdlog::logger>& GetCoreLogger() { RICE_BENCHMARK_FUNCTION(); return s_CoreLogger; }
            inline static Ref<spdlog::logger>& GetClientLogger() { RICE_BENCHMARK_FUNCTION(); return s_ClientLogger; }
        private:
            static Ref<spdlog::logger> s_CoreLogger;
            static Ref<spdlog::logger> s_ClientLogger;

    };
}

#define RICE_CORE_FATAL(...) ::Rice::Debug::GetCoreLogger()->critical(__VA_ARGS__) //__VA_ARGS__???? ?? ? ??
#define RICE_CORE_ERROR(...) ::Rice::Debug::GetCoreLogger()->error(__VA_ARGS__)
#define RICE_CORE_WARNING(...) ::Rice::Debug::GetCoreLogger()->warn(__VA_ARGS__)
#define RICE_CORE_INFO(...) ::Rice::Debug::GetCoreLogger()->info(__VA_ARGS__)
#define RICE_FATAL(...) ::Rice::Debug::GetClientLogger()->critical(__VA_ARGS__)
#define RICE_ERROR(...) ::Rice::Debug::GetClientLogger()->error(__VA_ARGS__)
#define RICE_LOG(...) ::Rice::Debug::GetClientLogger()->trace(__VA_ARGS__)
