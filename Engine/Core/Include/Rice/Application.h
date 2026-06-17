#pragma once

#include "Core.h"
#include "LayerStack.h"
#include "Event/Event.h"
#include "Event/ApplicationEvent.h"
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
        virtual void OnUpdate() {}
        virtual void OnRender() {}
        virtual void OnShutDown() {}

    private:
        bool OnWindowClose(WindowCloseEvent& e);

        bool m_Running = true;
        std::unique_ptr<Window> m_Window;
        LayerStack m_LayerStack;

        static Application* s_Instance;
    };

    Application* CreateApplication();
}
