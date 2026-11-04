#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "Rice/Layer.h"

namespace Rice
{
    Layer::Layer(const std::string& name)
        : m_DebugName(name)
    {
        RICE_BENCHMARK_FUNCTION();
    }

    Layer::~Layer()
    {
        RICE_BENCHMARK_FUNCTION();
        
    }
}
