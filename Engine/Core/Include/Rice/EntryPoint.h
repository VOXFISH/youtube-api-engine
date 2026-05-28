#pragma once

#ifdef _WIN32

extern Rice::Application* Rice::CreateApplication();

int main(int argc, char** argv)
{
    Rice::Debug::Init();

    std::cout<<"Hell World!"<<std::endl;
    auto app = Rice::CreateApplication();
    app -> Run();
    delete app;
}

#endif