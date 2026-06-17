#include <Rice.h>

#include "RainbowCube.h"

class SandboxApp : public Rice::Application
{
public:
    SandboxApp()
    {
        PushOverlay(new Rice::ImGuiLayer());
    }

    ~SandboxApp()
    {
    }

    void OnInit() override
    {
        m_RainbowCube.Init();
    }

    void OnUpdate() override
    {
        const Rice::Window& window = GetWindow();
        m_RainbowCube.Render(static_cast<float>(window.GetWidth()), static_cast<float>(window.GetHeight()));
    }

    void OnRender() override
    {
    }

    void OnShutDown() override
    {
        m_RainbowCube.Shutdown();
    }

private:
    RainbowCube m_RainbowCube;
};

Rice::Application* Rice::CreateApplication()
{
    return new SandboxApp();
}
