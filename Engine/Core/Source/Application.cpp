#include "Rice/PreCompiledHeaders.h"
#include "Rice/Application.h"
#include "Input/Input.h"
#include "Renderer/RenderCommand.h"

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
namespace Rice
{
    Application *Application::s_Instance = nullptr;

    Application::Application()
    {
        RICE_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        m_Window = Scope<Window>(Window::Create());
        /*
        Window  ??,  ?? ??????? ???        Window? ?  ? ????? ??? ??? ????        m_Window = CreateScope<WindowsWindow>();?  ??.
        ? Window ?? ?? ? WindowsWindow ???
        */
        m_Window->SetEventCallback(std::bind(&Application::OnEvent, this, std::placeholders::_1));
        // lectures.md .
    }

    Application::~Application()
    {
        if (s_Instance == this)
        {
            s_Instance = nullptr;
        }
    }

    Application &Application::Get()
    {
        RICE_CORE_ASSERT(s_Instance, "Application has not been created yet!");
        return *s_Instance;
    }

    void Application::PushLayer(Layer *layer)
    {
        m_LayerStack.PushLayer(layer);
        layer->OnAttach();
    }

    void Application::PushOverlay(Layer *overlay)
    {
        m_LayerStack.PushOverlay(overlay);
        overlay->OnAttach();
    }

    void Application::OnEvent(Event &e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(
            std::bind(&Application::OnWindowClose, this, std::placeholders::_1));

        for (auto it = m_LayerStack.end(); it != m_LayerStack.begin();)
        {
            (*--it)->OnEvent(e);
            if (e.Handled)
                break;
        }
    }

    void Application::Run()
    {
        OnInit();

        while (m_Running)
        {
            Time::Update((float)glfwGetTime());
            deltaTime = Time(Time::deltaTime);

            m_Window->OnUpdate();

            RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
            RenderCommand::Clear();

            OnUpdate(deltaTime);

            for (Layer *layer : m_LayerStack)
            {
                layer->OnUpdate(deltaTime);
                layer->OnImGuiRender();
            }

            OnRender();
            m_Window->SwapBuffers();
        }

        OnShutDown();
    }

    bool Application::OnWindowClose(WindowCloseEvent &e)
    {
        (void)e;
        m_Running = false;
        return true;
    }
}
