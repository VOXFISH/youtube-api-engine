#include "Rice/Benchmark.h"
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
        RICE_BENCHMARK_FUNCTION();
    }

    ImGuiLayer::~ImGuiLayer() = default;

    void ImGuiLayer::OnAttach()
    {
        RICE_BENCHMARK_FUNCTION();
        GLFWwindow* nativeWindow = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // Keep editor panels inside the single native application window.
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        ImGui_ImplGlfw_InitForOpenGL(nativeWindow, true);
        ImGui_ImplOpenGL3_Init("#version 330");
        m_Initialized = true;
    }

    void ImGuiLayer::OnDetach()
    {
        RICE_BENCHMARK_FUNCTION();
        if (m_Initialized)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            m_Initialized = false;
        }
    }

    void ImGuiLayer::OnUpdate(Time ts)
    {
        RICE_BENCHMARK_FUNCTION();
        (void)ts;

        if (!m_Initialized)
        {
            return;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        if(m_CanvasWidth>0 && m_CanvasHeight>0){
            auto& io=ImGui::GetIO();
            if(io.DisplaySize.x>0 && io.DisplaySize.y>0){
                io.MousePos.x*=m_CanvasWidth/io.DisplaySize.x;
                io.MousePos.y*=m_CanvasHeight/io.DisplaySize.y;
            }
            io.DisplaySize={float(m_CanvasWidth),float(m_CanvasHeight)};io.DisplayFramebufferScale={1,1};
        }
        ImGui::NewFrame();

        if (m_ShowDemoWindow)
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);

        if (m_RenderCallback)
            m_RenderCallback();

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
        RICE_BENCHMARK_FUNCTION();
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

    void ImGuiLayer::SetRenderCallback(std::function<void()> callback)
    {
        RICE_BENCHMARK_FUNCTION();
        m_RenderCallback = std::move(callback);
    }
}
