#pragma once

#include "Rice/Benchmark.h"
#include "Rice/Core.h"
#include "Layer.h"

#include <vector>

namespace Rice
{
    class RICE_API LayerStack
    {
    private:
        std::vector<Layer*> m_Layers;
        size_t m_LayerInsert = 0;
    public:
        LayerStack();
        ~LayerStack();

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* overlay);
        void PopLayer(Layer* layer);
        void PopOverlay(Layer* overlay);

        std::vector<Layer*>::iterator begin() { RICE_BENCHMARK_FUNCTION(); return m_Layers.begin();}
        std::vector<Layer*>::iterator end() { RICE_BENCHMARK_FUNCTION(); return m_Layers.end();}
    };
}