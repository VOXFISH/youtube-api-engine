#include "Rice/PreCompiledHeaders.h"
#include "ImGuiLayer.h"

#include "../Rice/Application.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace Rice
{
    ImGuiLayer::ImGuiLayer()
        : Layer("ImGuiLayer")
    {
    }

    ImGuiLayer::~ImGuiLayer() = default;

    void ImGuiLayer::OnAttach()
    {
        GLFWwindow* nativeWindow = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        ImGui_ImplGlfw_InitForOpenGL(nativeWindow, true);
        ImGui_ImplOpenGL3_Init("#version 410");
        m_Initialized = true;
    }

    void ImGuiLayer::OnDetach()
    {
        if (m_Initialized)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            m_Initialized = false;
        }
    }

    void ImGuiLayer::OnUpdate()
    {
        if (!m_Initialized)
        {
            return;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::ShowDemoWindow(&m_ShowDemoWindow);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            GLFWwindow* currentContext = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(currentContext);
        }
    }

    void ImGuiLayer::OnEvent(Event& event)
    {
        if (!m_Initialized)
        {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        if (event.IsInCategory(EventCategoryMouse) && io.WantCaptureMouse)
        {
            event.Handled = true;
        }
        if (event.IsInCategory(EventCategoryKeyboard) && io.WantCaptureKeyboard)
        {
            event.Handled = true;
        }
    }
}
