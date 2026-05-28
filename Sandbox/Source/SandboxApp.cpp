#include <Rice.h>

class SandboxApp : public Rice::Application
{
public:
    SandboxApp()
    {
    }

    ~SandboxApp()
    {
    }

    void OnInit() override
    {
    }

    void OnUpdate() override
    {
    }

    void OnRender() override
    {
    }

    void OnShutDown() override
    {
    }
};

Rice::Application* Rice::CreateApplication()
{
    return new SandboxApp();
}