#pragma once

#include "Application.h"
#include "Benchmark.h"
#include "Debug.h"

namespace Rice {
    Application* CreateApplication();
}

#ifdef _WIN32

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    Rice::Debug::Init();
    Rice::BenchmarkSession benchmark("Logs/functions.tsv");
    RICE_CORE_INFO("Rice Engine Initiated.");
    RICE_LOG("Hell World!");

    auto* app = Rice::CreateApplication();
    app->Run();
    delete app;
    return 0;
}

#endif
