#include "Rice/PreCompiledHeaders.h"
#include "WindowsWindow.h"

#include "Event/MouseEvent.h"
#include "Event/KeyEvent.h"
#include "Event/ApplicationEvent.h"

#include <glad/glad.h>
#include <glfw/glfw3.h>


namespace Rice
{
    static bool s_GLFWInitialized = false;

    static void GLFWErrorCallback(int error, const char* description)
    {
        RICE_CORE_WARNING("GLFW Error({0}): {1}", error, description);
    }

    Window* Window::Create(const WindowProps& props)
    {
        return new WindowsWindow(props);
    }

    WindowsWindow::WindowsWindow(const WindowProps& props)
    {
        Init(props);
    }

    WindowsWindow::~WindowsWindow()
    {
        Shutdown();
    }

    void WindowsWindow::Init(const WindowProps& props)
    {

        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        RICE_CORE_INFO("Creating window {0} ({1}, {2})", props.Title, props.Width, props.Height);

        if(!s_GLFWInitialized)
        {
            int success = glfwInit();
            RICE_CORE_ASSERT(success, "Could not initialize GLFW!");
            (void)success;
            glfwSetErrorCallback(GLFWErrorCallback);
            s_GLFWInitialized = true;
        }

        m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
        
        glfwMakeContextCurrent(m_Window);
        int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        RICE_CORE_ASSERT(status, "Failed to initialize GLAD");
        (void)status;
        glfwSetWindowUserPointer(m_Window, &m_Data);
        SetVSync(true);

        RICE_CORE_INFO("OpenGL Info: ");
        RICE_CORE_INFO("Vendor: {0}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
        RICE_CORE_INFO("Renderer: {0}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        RICE_CORE_INFO("Version: {0}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

        // GLFW callbacks
        glfwSetWindowSizeCallback(
            m_Window, [](GLFWwindow* window, int width, int height)
            {
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
                data.Width = width;
                data.Height = height;

                WindowResizeEvent event(width, height);
                data.EventCallback(event);
            }
        );

        glfwSetWindowCloseCallback(
            m_Window, [](GLFWwindow* window)
            {
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
                WindowCloseEvent event;
                data.EventCallback(event);
            }
        );

        glfwSetKeyCallback(
            m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
            {
                (void)scancode;
                (void)mods;
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

                switch(action)
                {
                    case GLFW_PRESS:
                    {
                        KeyPressedEvent event(static_cast<KeyCode>(key), false);
                        data.EventCallback(event);
                        break;
                    }
                    case GLFW_RELEASE:
                    {
                        KeyReleasedEvent event(static_cast<KeyCode>(key));
                        data.EventCallback(event);
                        break;
                    }
                    case GLFW_REPEAT:
                    {
                        KeyPressedEvent event(static_cast<KeyCode>(key), true);
                        data.EventCallback(event);
                        break;
                    }
                }
            }
        );

        glfwSetCharCallback(
            m_Window, [](GLFWwindow* window, unsigned int codepoint)
            {
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

                KeyTypedEvent event(codepoint);
                data.EventCallback(event);
            }
        );

        glfwSetMouseButtonCallback(
            m_Window, [](GLFWwindow* window, int button, int action, int mods)
            {
                (void)mods;
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

                switch(action)
                {
                    case GLFW_PRESS:
                    {
                        MouseButtonPressedEvent event(static_cast<MouseCode>(button));
                        data.EventCallback(event);
                        break;
                    }
                    case GLFW_RELEASE:
                    {
                        MouseButtonReleasedEvent event(static_cast<MouseCode>(button));
                        data.EventCallback(event);
                        break;
                    }
                }
            }
        );

        glfwSetScrollCallback(
            m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
            {
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

                MouseScrolledEvent event((float)xOffset, (float)yOffset);
                data.EventCallback(event);
            }
        );

        glfwSetCursorPosCallback(
            m_Window, [](GLFWwindow* window, double xPos, double yPos)
            {
                WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

                MouseMovedEvent event((float)xPos, (float)yPos);
                data.EventCallback(event);
            }
        );
    }

    void WindowsWindow::Shutdown()
    {
        glfwDestroyWindow(m_Window);
    }

    void WindowsWindow::OnUpdate()
    {
        glfwPollEvents();
    }

    void WindowsWindow::SwapBuffers()
    {
        glfwSwapBuffers(m_Window);
    }

    void WindowsWindow::SetVSync(bool enabled)
    {
        enabled ? glfwSwapInterval(1) : glfwSwapInterval(0);
        m_Data.VSync = enabled;
    }

    bool WindowsWindow::IsVSync() const
    {
        return m_Data.VSync;
    }
}
