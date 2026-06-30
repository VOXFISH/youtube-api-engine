#pragma once
#include "Rice/Layer.h"
#include "Event/MouseEvent.h"
#include "Event/KeyEvent.h"

#include <functional>

namespace Rice
{
    class RICE_API ImGuiLayer : public Layer
    {
    private:
        bool m_ShowDemoWindow = false;
        bool m_Initialized = false;
        std::function<void()> m_RenderCallback;

    public:
        ImGuiLayer();
        ~ImGuiLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Time ts) override;
        void OnEvent(Event &event) override;

        void SetRenderCallback(std::function<void()> callback);
    };
}
