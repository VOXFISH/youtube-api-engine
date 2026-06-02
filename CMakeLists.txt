cmake_minimum_required(VERSION 3.23)

set(_vcpkg_toolchain_file "")

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    if(EXISTS "${CMAKE_TOOLCHAIN_FILE}")
        set(_vcpkg_toolchain_file "${CMAKE_TOOLCHAIN_FILE}")
    else()
        message(STATUS "Ignoring missing CMAKE_TOOLCHAIN_FILE: ${CMAKE_TOOLCHAIN_FILE}")
        unset(CMAKE_TOOLCHAIN_FILE CACHE)
        unset(CMAKE_TOOLCHAIN_FILE)
    endif()
endif()

if(NOT _vcpkg_toolchain_file AND DEFINED ENV{VCPKG_ROOT})
    file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _vcpkg_root)
    set(_vcpkg_toolchain_candidates
        "${_vcpkg_root}/vcpkg.cmake"
        "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
    )

    foreach(_candidate IN LISTS _vcpkg_toolchain_candidates)
        if(EXISTS "${_candidate}")
            set(_vcpkg_toolchain_file "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(_vcpkg_toolchain_file AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${_vcpkg_toolchain_file}" CACHE FILEPATH "vcpkg toolchain file")
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

unset(_candidate)
unset(_vcpkg_root)
unset(_vcpkg_toolchain_candidates)
unset(_vcpkg_toolchain_file)

project(PROJECT-RICE
    VERSION 0.0.0.0
    DESCRIPTION "Rice engine"
    LANGUAGES C CXX)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(engine/Core)
add_subdirectory(Sandbox)
