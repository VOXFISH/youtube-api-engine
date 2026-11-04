#pragma once

#include "Rice/Benchmark.h"
#include "Rice/Core.h"
#include "Rice/Time.h"
#include "Event/Event.h"

namespace Rice
{
    class RICE_API Layer
    {
    public:
        Layer(const std::string& name = "Layer");
        virtual ~Layer();

        virtual void OnAttach() { RICE_BENCHMARK_FUNCTION(); }
        virtual void OnDetach() { RICE_BENCHMARK_FUNCTION(); }
        virtual void OnUpdate(Time) { RICE_BENCHMARK_FUNCTION(); }
        virtual void OnImGuiRender() { RICE_BENCHMARK_FUNCTION(); }
        virtual void OnEvent(Event&) { RICE_BENCHMARK_FUNCTION(); }

        inline const std::string& Getname() const { RICE_BENCHMARK_FUNCTION(); return m_DebugName;}

    protected:
        std::string m_DebugName;
    };
}
