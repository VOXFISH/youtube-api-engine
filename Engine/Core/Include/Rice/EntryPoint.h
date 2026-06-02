#pragma once

namespace Rice {
    class Application;
    namespace Debug { void Init(); }
    Application* CreateApplication();
}
#ifdef _WIN32

extern Rice::Application* Rice::CreateApplication();

int main(int argc, char** argv)
{
    Rice::Debug::Init();
    RICE_CORE_FATAL("치명적인 테스트");
    RICE_CORE_ERROR("조금 덜 치명적인 테스트");
    RICE_CORE_WARNING("상당히 위험한 테스트");
    RICE_LOG("Hell World!");

    auto app = Rice::CreateApplication();
    app->Run();
    delete app;
}

#endif
