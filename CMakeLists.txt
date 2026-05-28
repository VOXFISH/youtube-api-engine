cmake_minimum_required(VERSION 3.23)
project(PROJECT-RICE
    VERSION 0.0.0.0
    DESCRIPTION "유튜브쌀먹엔진"
    LANGUAGES C CXX)

    add_subdirectory(Engine/Core)
    add_subdirectory(Sandbox)
    add_subdirectory(Dependencies/spdlog)