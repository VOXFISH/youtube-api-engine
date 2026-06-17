#include "Rice/PreCompiledHeaders.h"
#include "WindowsInput.h"
#include "Rice/Application.h"
#include <GLFW/glfw3.h>

namespace Rice
{
    Input* Input::s_Instance = new WindowsInput();

    bool WindowsInput::IsKeyPressedImpl(KeyCode keyCode)
    {
        auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        auto state = glfwGetKey(window, static_cast<int>(keyCode));
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool WindowsInput::IsMouseButtonPressedImpl(MouseCode button)
    {
        auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        auto state = glfwGetMouseButton(window, static_cast<int>(button));
        return state == GLFW_PRESS;
    }

    std::pair<float, float> WindowsInput::GetMousePositionImpl()
    {
        auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        return { (float)xpos, (float)ypos };
    }

    float WindowsInput::GetMouseXImpl()
    {
        auto [x, y] = GetMousePositionImpl();
        (void)y;
        return x;
    }

    float WindowsInput::GetMouseYImpl()
    {
        auto [x, y] = GetMousePositionImpl();
        (void)x;
        return y;
    }
}
