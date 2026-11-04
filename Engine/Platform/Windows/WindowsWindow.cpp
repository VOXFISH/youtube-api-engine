#include "Rice/Benchmark.h"
#include "Rice/PreCompiledHeaders.h"
#include "WindowsWindow.h"

#include "Event/MouseEvent.h"
#include "Event/KeyEvent.h"
#include "Event/ApplicationEvent.h"

#include <stdexcept>
#include <glad/glad.h>
#include <glfw/glfw3.h>


namespace Rice
{
    static bool s_GLFWInitialized = false;

    static void GLFWErrorCallback(int error, const char* description)
    {
        RICE_BENCHMARK_FUNCTION();
        RICE_CORE_WARNING("GLFW Error({0}): {1}", error, description);
    }

    Window* Window::Create(const WindowProps& props)
    {
        RICE_BENCHMARK_FUNCTION();
        return new WindowsWindow(props);
    }

    WindowsWindow::WindowsWindow(const WindowProps& props)
    {
        RICE_BENCHMARK_FUNCTION();
        Init(props);
    }

    WindowsWindow::~WindowsWindow()
    {
        RICE_BENCHMARK_FUNCTION();
        Shutdown();
    }

    void WindowsWindow::Init(const WindowProps& props)
    {
        RICE_BENCHMARK_FUNCTION();

        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        RICE_CORE_INFO("Creating window {0} ({1}, {2})", props.Title, props.Width, props.Height);

        if(!s_GLFWInitialized)
        {
            int success = glfwInit();
            if (!success) throw std::runtime_error("Could not initialize GLFW");
            (void)success;
            glfwSetErrorCallback(GLFWErrorCallback);
            s_GLFWInitialized = true;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
        
        if (!m_Window) throw std::runtime_error("Could not create OpenGL window");
        glfwMakeContextCurrent(m_Window);
        int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        if (!status) { glfwDestroyWindow(m_Window); throw std::runtime_error("Failed to initialize GLAD"); }
        (void)status;
        glfwSetWindowUserPointer(m_Window, &m_Data);
        SetVSync(true);

        RICE_CORE_INFO("OpenGL Info: ");
        RICE_CORE_INFO("Vendor: {0}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
        RICE_CORE_INFO("Renderer: {0}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        RICE_CORE_INFO("Version: {0}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

        glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow*, int w, int h) { RICE_BENCHMARK_FUNCTION(); glViewport(0, 0, w, h); });
        // GLFW callbacks
        glfwSetWindowSizeCallback(
            m_Window, [](GLFWwindow* window, int width, int height)
            {
                RICE_BENCHMARK_FUNCTION();
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
                data.Width = width;
                data.Height = height;

                WindowResizeEvent event(width, height);
                if (data.EventCallback) data.EventCallback(event);
            }
        );

        glfwSetWindowCloseCallback(
            m_Window, [](GLFWwindow* window)
            {
                RICE_BENCHMARK_FUNCTION();
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
                WindowCloseEvent event;
                if (data.EventCallback) data.EventCallback(event);
            }
        );

        glfwSetKeyCallback(
            m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
            {
                RICE_BENCHMARK_FUNCTION();
                (void)scancode;
                (void)mods;
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

                switch(action)
                {
                    case GLFW_PRESS:
                    {
                        KeyPressedEvent event(static_cast<KeyCode>(key), false);
                        if (data.EventCallback) data.EventCallback(event);
                        break;
                    }
                    case GLFW_RELEASE:
                    {
                        KeyReleasedEvent event(static_cast<KeyCode>(key));
                        if (data.EventCallback) data.EventCallback(event);
                        break;
                    }
                    case GLFW_REPEAT:
                    {
                        KeyPressedEvent event(static_cast<KeyCode>(key), true);
                        if (data.EventCallback) data.EventCallback(event);
                        break;
                    }
                }
            }
        );

        glfwSetCharCallback(
            m_Window, [](GLFWwindow* window, unsigned int codepoint)
            {
                RICE_BENCHMARK_FUNCTION();
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

                KeyTypedEvent event(codepoint);
                if (data.EventCallback) data.EventCallback(event);
            }
        );

        glfwSetMouseButtonCallback(
            m_Window, [](GLFWwindow* window, int button, int action, int mods)
            {
                RICE_BENCHMARK_FUNCTION();
                (void)mods;
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

                switch(action)
                {
                    case GLFW_PRESS:
                    {
                        MouseButtonPressedEvent event(static_cast<MouseCode>(button));
                        if (data.EventCallback) data.EventCallback(event);
                        break;
                    }
                    case GLFW_RELEASE:
                    {
                        MouseButtonReleasedEvent event(static_cast<MouseCode>(button));
                        if (data.EventCallback) data.EventCallback(event);
                        break;
                    }
                }
            }
        );

        glfwSetScrollCallback(
            m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
            {
                RICE_BENCHMARK_FUNCTION();
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

                MouseScrolledEvent event((float)xOffset, (float)yOffset);
                if (data.EventCallback) data.EventCallback(event);
            }
        );

        glfwSetCursorPosCallback(
            m_Window, [](GLFWwindow* window, double xPos, double yPos)
            {
                RICE_BENCHMARK_FUNCTION();
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

                MouseMovedEvent event((float)xPos, (float)yPos);
                if (data.EventCallback) data.EventCallback(event);
            }
        );
    }

    void WindowsWindow::Shutdown()
    {
        RICE_BENCHMARK_FUNCTION();
        glfwDestroyWindow(m_Window);
    }

    void WindowsWindow::OnUpdate()
    {
        RICE_BENCHMARK_FUNCTION();
        glfwPollEvents();
    }

    void WindowsWindow::SwapBuffers()
    {
        RICE_BENCHMARK_FUNCTION();
        glfwSwapBuffers(m_Window);
    }

    void WindowsWindow::SetVSync(bool enabled)
    {
        RICE_BENCHMARK_FUNCTION();
        enabled ? glfwSwapInterval(1) : glfwSwapInterval(0);
        m_Data.VSync = enabled;
    }

    bool WindowsWindow::IsVSync() const
    {
        RICE_BENCHMARK_FUNCTION();
        return m_Data.VSync;
    }
}
