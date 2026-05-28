#pragma once

#include "Core.h"
namespace Rice
{
    class RICE_API Application
    {
    public:
        Application();
        ~Application();

        void Run();
    

    protected:
        virtual void OnInit() {}
        virtual void OnUpdate() {}
        virtual void OnRender() {}
        virtual void OnShutDown() {}

    private:
        bool m_Running = true;
    };

    Application* CreateApplication();
}