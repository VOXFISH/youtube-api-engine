#include "Rice/PreCompiledHeaders.h"
#include "Rice/LayerStack.h"

namespace Rice
{
    /*
    event는 layer를 역순으로 순회함.
    항상 layer 위에 overlay가 쌓여서 overlay는 모든 layer 위에 적용됨.
    layer는 application 종료 시 layerstack이 사라질 때까지 사라지지 않음
    대신 stack에서 뺄 수 있음
    */
    LayerStack::LayerStack()
    {
        m_LayerInsert = m_Layers.begin();
    }

    LayerStack::~LayerStack()
    {
        for (Layer* layer : m_Layers)
        {
            layer->OnDetach();
            delete layer;
        }
    }

    void LayerStack::PushLayer(Layer* layer)
    {
        m_LayerInsert = m_Layers.emplace(m_LayerInsert, layer);
    }

    void LayerStack::PushOverlay(Layer* overlay)
    {
        m_Layers.emplace_back(overlay);
    }

    void LayerStack::PopLayer(Layer* layer)
    {
        auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);
        if(it!=m_Layers.end())
        {
            m_Layers.erase(it);
            m_LayerInsert--;
        }
    }

    void LayerStack::PopOverlay(Layer* overlay)
    {
        auto it = std::find(m_Layers.begin(), m_Layers.end(), overlay);
        if(it != m_Layers.end()) m_Layers.erase(it);
    }
}
