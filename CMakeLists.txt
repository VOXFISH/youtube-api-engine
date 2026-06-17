cmake_minimum_required(VERSION 3.25)

set(_rice_vcpkg_toolchain "")

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    if(EXISTS "${CMAKE_TOOLCHAIN_FILE}")
        set(_rice_vcpkg_toolchain "${CMAKE_TOOLCHAIN_FILE}")
    else()
        message(STATUS "Ignoring missing CMAKE_TOOLCHAIN_FILE: ${CMAKE_TOOLCHAIN_FILE}")
        unset(CMAKE_TOOLCHAIN_FILE CACHE)
        unset(CMAKE_TOOLCHAIN_FILE)
    endif()
endif()

if(NOT _rice_vcpkg_toolchain AND DEFINED ENV{VCPKG_ROOT})
    file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _vcpkg_root)
    set(_vcpkg_toolchain_candidates
        "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
        "${_vcpkg_root}/vcpkg.cmake"
    )

    foreach(_candidate IN LISTS _vcpkg_toolchain_candidates)
        if(EXISTS "${_candidate}")
            set(_rice_vcpkg_toolchain "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(_rice_vcpkg_toolchain AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${_rice_vcpkg_toolchain}" CACHE FILEPATH "vcpkg toolchain file")
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR
        "vcpkg toolchain not found. Set VCPKG_ROOT to the vcpkg installation directory "
        "or configure with -DCMAKE_TOOLCHAIN_FILE=<path>/vcpkg.cmake."
    )
endif()

unset(_candidate)
unset(_vcpkg_root)
unset(_vcpkg_toolchain_candidates)
unset(_rice_vcpkg_toolchain)

project(PROJECT-RICE
    VERSION 0.0.0.0
    DESCRIPTION "Rice engine"
    LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "PROJECT-RICE currently supports Windows only.")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(RICE_BUILD_SANDBOX "Build the sandbox application" ON)

add_library(RiceBuildOptions INTERFACE)
target_compile_features(RiceBuildOptions INTERFACE cxx_std_20)
target_compile_definitions(RiceBuildOptions INTERFACE GLFW_INCLUDE_NONE)

if(MSVC)
    target_compile_options(RiceBuildOptions INTERFACE /utf-8 /W4 /EHsc)
else()
    target_compile_options(RiceBuildOptions INTERFACE -Wall -Wextra -Wpedantic)
endif()

find_package(spdlog CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_subdirectory(Engine/Core)
add_subdirectory(Engine/Input)
add_subdirectory(Engine/Platform)
add_subdirectory(Engine/Renderer)

if(RICE_BUILD_SANDBOX)
    add_subdirectory(Sandbox)
endif()
