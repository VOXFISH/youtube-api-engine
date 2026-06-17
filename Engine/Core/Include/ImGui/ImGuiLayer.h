#pragma once
#include "Rice/Layer.h"
#include "Event/MouseEvent.h"
#include "Event/KeyEvent.h"

namespace Rice
{
    class RICE_API ImGuiLayer : public Layer
    {
    private:
        bool m_ShowDemoWindow = true;
        bool m_Initialized = false;

    public:
        ImGuiLayer();
        ~ImGuiLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate() override;
        void OnEvent(Event &event) override;
    };
}
