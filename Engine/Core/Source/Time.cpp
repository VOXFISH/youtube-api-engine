#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "Rice/Time.h"

namespace Rice
{
    float Time::time = 0.0f;
    float Time::deltaTime = 0.0f;
    float Time::unscaledTime = 0.0f;
    float Time::unscaledDeltaTime = 0.0f;
    float Time::frameTime = 0.0f;
    Time::TimeScale Time::timeScale = {};

    Time::TimeScale& Time::TimeScale::operator=(float scale)
    {
        RICE_BENCHMARK_FUNCTION();
        if (scale < 0.0f)
        {
            RICE_CORE_WARNING("timeScale cannot be negative. Clamped to 0.");
            value = 0.0f;
        }
        else
        {
            value = scale;
        }

        return *this;
    }

    void Time::Update(float currentTime)
    {
        RICE_BENCHMARK_FUNCTION();
        unscaledDeltaTime = currentTime - frameTime;
        frameTime = currentTime;

        deltaTime = unscaledDeltaTime * timeScale;

        unscaledTime = currentTime;
        time += deltaTime;
    }
}
