cmake_minimum_required(VERSION 3.23)

project(PROJECT-RICE
    VERSION 0.0.0.0
    DESCRIPTION "Rice engine"
    LANGUAGES C CXX)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(engine/Core)
add_subdirectory(Sandbox)
