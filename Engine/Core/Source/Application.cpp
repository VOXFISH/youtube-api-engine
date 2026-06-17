#include "Rice/PreCompiledHeaders.h"
#include "Rice/Application.h"
#include "Input/Input.h"
#include "Renderer/RenderCommand.h"

#include <glm/glm.hpp>
namespace Rice
{
    Application *Application::s_Instance = nullptr;

    Application::Application()
    {
        RICE_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        m_Window = std::unique_ptr<Window>(Window::Create());
        /*
        Window가 추상 클래스고, 추상 클래스는 객체를 생성할 수 없기 때문에
        Window를 멤버 변수로 직접 두는 대신 포인터에 바인딩하고 실제 객체는 자식 클래스에서
        m_Window = std::make_unique<WindowsWindow>();와 같이 생성한다.
        이때 Window 타입의 포인터는 실제로는 WindowsWindow 객체를 가리킨다.
        */
        m_Window->SetEventCallback(std::bind(&Application::OnEvent, this, std::placeholders::_1));
        // lectures.md 참고.
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
            m_Window->OnUpdate();

            RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
            RenderCommand::Clear();

            OnUpdate();

            for (Layer *layer : m_LayerStack)
            {
                layer->OnUpdate();
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
