#pragma once

#include "Core.h"
#include <memory>
#include <spdlog/spdlog.h>

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
