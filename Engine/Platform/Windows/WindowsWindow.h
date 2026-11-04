#pragma once

#include "Rice/Benchmark.h"
#include "Rice/Window.h"

struct GLFWwindow;

namespace Rice
{
    class WindowsWindow : public Window
    {
    public:
        WindowsWindow(const WindowProps& props);
        virtual ~WindowsWindow();

        void OnUpdate() override;
        void SwapBuffers() override;

        inline unsigned int GetWidth() const override { RICE_BENCHMARK_FUNCTION(); return m_Data.Width; }
        inline unsigned int GetHeight() const override { RICE_BENCHMARK_FUNCTION(); return m_Data.Height; }
        inline void* GetNativeWindow() const override { RICE_BENCHMARK_FUNCTION(); return m_Window; }

        inline void SetEventCallback(const EventCallbackFn& callback) override { RICE_BENCHMARK_FUNCTION(); m_Data.EventCallback = callback; }
        void SetVSync(bool enabled);
        bool IsVSync() const override;

    private:
        virtual void Init(const WindowProps& props);
        virtual void Shutdown();
        GLFWwindow* m_Window;

        struct WindowData //GLFW ???? ??? ?? ?
        {
            std::string Title;
            unsigned int Width, Height;
            bool VSync;

            EventCallbackFn EventCallback;
        };

        WindowData m_Data;
    };
}
