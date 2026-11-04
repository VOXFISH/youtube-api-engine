#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "Rice/LayerStack.h"
namespace Rice {
LayerStack::LayerStack() = default;
LayerStack::~LayerStack(){ RICE_BENCHMARK_FUNCTION(); for(auto* layer:m_Layers){layer->OnDetach();delete layer;}}
void LayerStack::PushLayer(Layer* layer){ RICE_BENCHMARK_FUNCTION(); m_Layers.insert(m_Layers.begin()+m_LayerInsert,layer);++m_LayerInsert;}
void LayerStack::PushOverlay(Layer* layer){ RICE_BENCHMARK_FUNCTION(); m_Layers.push_back(layer);}
void LayerStack::PopLayer(Layer* layer){ RICE_BENCHMARK_FUNCTION(); auto boundary=m_Layers.begin()+m_LayerInsert;auto it=std::find(m_Layers.begin(),boundary,layer);if(it!=boundary){layer->OnDetach();m_Layers.erase(it);--m_LayerInsert;delete layer;}}
void LayerStack::PopOverlay(Layer* layer){ RICE_BENCHMARK_FUNCTION(); auto it=std::find(m_Layers.begin()+m_LayerInsert,m_Layers.end(),layer);if(it!=m_Layers.end()){layer->OnDetach();m_Layers.erase(it);delete layer;}}
}
