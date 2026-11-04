#pragma once

#include "Rice/Benchmark.h"
#include "Core.h"

namespace Rice
{
    class RICE_API Time
    {
    public:
        struct TimeScale
        {
            float value = 1.0f;

            TimeScale& operator=(float scale);
            operator float() const { RICE_BENCHMARK_FUNCTION(); return value; }
        };

        Time(float t = 0.0f)
            : m_Time(t)
        {
            RICE_BENCHMARK_FUNCTION();
        }

        static float time;
        static float deltaTime;
        static constexpr float fixedDeltaTime = 1.0f / 60.0f;
        static float unscaledTime;
        static float unscaledDeltaTime;
        static TimeScale timeScale;

        float GetSeconds() const { RICE_BENCHMARK_FUNCTION(); return m_Time; }
        float GetTimeMiliseconds() const { RICE_BENCHMARK_FUNCTION(); return m_Time * 1000.0f; }

    private:
        friend class Application;

        static void Update(float currentTime);

        static float frameTime;

        float m_Time;
    };
}
