#pragma once

#include "Rice/Benchmark.h"
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
        explicit Application(const WindowProps& props = WindowProps());
        virtual ~Application();

        static Application& Get();

        void Run();
        void Close() { RICE_BENCHMARK_FUNCTION(); m_Running = false; }
    
        void OnEvent(Event& e);

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* layer);
        Window& GetWindow() const { RICE_BENCHMARK_FUNCTION(); return *m_Window; }
    protected:
        virtual void OnInit() { RICE_BENCHMARK_FUNCTION(); }
        virtual void OnUpdate(Time) { RICE_BENCHMARK_FUNCTION(); }
        virtual void OnRender() { RICE_BENCHMARK_FUNCTION(); }
        virtual void OnFrameRendered() {}
        virtual void OnCloseRequested() { Close(); }
        virtual void OnShutDown() { RICE_BENCHMARK_FUNCTION(); }

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
