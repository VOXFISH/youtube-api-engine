#pragma once

#include "Core.h"
#include "LayerStack.h"
#include "Event/Event.h"
#include "Event/ApplicationEvent.h"
#include "Time.h"
#include "Window.h"

namespace Rice
{
    class RICE_API Application
    {
    public:
        Application();
        ~Application();

        static Application& Get();

        void Run();
    
        void OnEvent(Event& e);

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* layer);
        Window& GetWindow() const { return *m_Window; }
    protected:
        virtual void OnInit() {}
        virtual void OnUpdate(Time) {}
        virtual void OnRender() {}
        virtual void OnShutDown() {}

    private:
        bool OnWindowClose(WindowCloseEvent& e);

        bool m_Running = true;
        Scope<Window> m_Window;
        LayerStack m_LayerStack;
        Time deltaTime;
        float m_LastFrameTime = 0.0f;

        static Application* s_Instance;
    };

    Application* CreateApplication();
}
