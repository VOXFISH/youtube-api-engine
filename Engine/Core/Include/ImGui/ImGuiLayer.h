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
        int m_CanvasWidth=0,m_CanvasHeight=0;

    public:
        ImGuiLayer();
        ~ImGuiLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Time ts) override;
        void OnEvent(Event &event) override;

        void SetRenderCallback(std::function<void()> callback);
        void SetCanvasSize(int width,int height){m_CanvasWidth=width;m_CanvasHeight=height;}
    };
}
