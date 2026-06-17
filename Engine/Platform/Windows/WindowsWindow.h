#pragma once

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

        inline unsigned int GetWidth() const override { return m_Data.Width; }
        inline unsigned int GetHeight() const override { return m_Data.Height; }
        inline void* GetNativeWindow() const override { return m_Window; }

        inline void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
        void SetVSync(bool enabled);
        bool IsVSync() const override;

    private:
        virtual void Init(const WindowProps& props);
        virtual void Shutdown();
        GLFWwindow* m_Window;

        struct WindowData //GLFW가 요청할 실제 데이터를 보관하는 영역
        {
            std::string Title;
            unsigned int Width, Height;
            bool VSync;

            EventCallbackFn EventCallback;
        };

        WindowData m_Data;
    };
}
