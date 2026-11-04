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

option(RICE_BUILD_SANDBOX "Build the Rice editor and standalone player" ON)

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
add_subdirectory(Engine/Audio)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
include(InstallRequiredSystemLibraries)

if(RICE_BUILD_SANDBOX)
    add_subdirectory(Sandbox)
endif()

include(CTest)
if(BUILD_TESTING)
    foreach(target RiceInstancingTests RiceInstancingBenchmark)
        if(target STREQUAL "RiceInstancingTests")
            set(source Tests/InstancingTests.cpp)
        else()
            set(source Tests/InstancingBenchmark.cpp)
        endif()
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE RiceCore RiceBuildOptions glm::glm glfw glad::glad)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS)
    endforeach()
    add_test(NAME CubeInstancing COMMAND RiceInstancingTests)
endif()
if(BUILD_TESTING)
    foreach(test_name Workspace Prefab)
        add_executable(Rice${test_name}Tests Tests/${test_name}Tests.cpp)
        target_link_libraries(Rice${test_name}Tests PRIVATE RiceCore RiceBuildOptions glm::glm)
        add_test(NAME ${test_name}Lifecycle COMMAND Rice${test_name}Tests "${CMAKE_BINARY_DIR}/TestData/${test_name}")
        add_custom_command(TARGET Rice${test_name}Tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:Rice${test_name}Tests> $<TARGET_FILE_DIR:Rice${test_name}Tests>
            COMMAND_EXPAND_LISTS)
    endforeach()
endif()
if(BUILD_TESTING)
    add_executable(RiceSceneTests Tests/SceneTests.cpp)
    target_link_libraries(RiceSceneTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME SceneLifecycle COMMAND RiceSceneTests)
endif()

if(BUILD_TESTING)
    add_executable(RiceEngineTests Tests/EngineTests.cpp)
    target_link_libraries(RiceEngineTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME EngineIntegration COMMAND RiceEngineTests "${CMAKE_BINARY_DIR}/TestData" "${CMAKE_SOURCE_DIR}/Sandbox/Assets/tone.wav")
    add_custom_command(TARGET RiceEngineTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceEngineTests> $<TARGET_FILE_DIR:RiceEngineTests>
        COMMAND_EXPAND_LISTS)
endif()

install(FILES docs/EngineManual.md docs/EngineFunctions.md DESTINATION docs)
if(BUILD_TESTING)
    add_executable(RiceLogTests Tests/LogTests.cpp)
    target_link_libraries(RiceLogTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME EditorLogRouting COMMAND RiceLogTests)
    add_custom_command(TARGET RiceLogTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceLogTests> $<TARGET_FILE_DIR:RiceLogTests>
        COMMAND_EXPAND_LISTS)
endif()
if(BUILD_TESTING)
    add_executable(RiceBenchmarkTests Tests/BenchmarkTests.cpp)
    target_link_libraries(RiceBenchmarkTests PRIVATE RiceCore RiceBuildOptions)
    add_test(NAME FunctionBenchmark COMMAND RiceBenchmarkTests "${CMAKE_BINARY_DIR}/TestData/Benchmark")
    add_custom_command(TARGET RiceBenchmarkTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceBenchmarkTests> $<TARGET_FILE_DIR:RiceBenchmarkTests>
        COMMAND_EXPAND_LISTS)
    add_executable(RiceBenchmarkPanelTests Tests/BenchmarkPanelTests.cpp)
    target_link_libraries(RiceBenchmarkPanelTests PRIVATE RiceCore RiceBuildOptions imgui::imgui)
    add_test(NAME BenchmarkWindow COMMAND RiceBenchmarkPanelTests "${CMAKE_BINARY_DIR}/TestData/BenchmarkWindow")
    add_custom_command(TARGET RiceBenchmarkPanelTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceBenchmarkPanelTests> $<TARGET_FILE_DIR:RiceBenchmarkPanelTests>
        COMMAND_EXPAND_LISTS)
endif()
if(BUILD_TESTING)
    add_executable(RiceGizmoTests Tests/GizmoTests.cpp)
    target_link_libraries(RiceGizmoTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME TransformGizmo COMMAND RiceGizmoTests)
    add_custom_command(TARGET RiceGizmoTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceGizmoTests> $<TARGET_FILE_DIR:RiceGizmoTests>
        COMMAND_EXPAND_LISTS)
endif()
if(BUILD_TESTING)
    add_executable(RiceClipboardTests Tests/ClipboardTests.cpp)
    target_link_libraries(RiceClipboardTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME EditorClipboard COMMAND RiceClipboardTests)
    add_custom_command(TARGET RiceClipboardTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceClipboardTests> $<TARGET_FILE_DIR:RiceClipboardTests>
        COMMAND_EXPAND_LISTS)
endif()

if(BUILD_TESTING)
    add_executable(RiceGroupTests Tests/GroupTests.cpp)
    target_link_libraries(RiceGroupTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME EditorGroups COMMAND RiceGroupTests)
    add_custom_command(TARGET RiceGroupTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceGroupTests> $<TARGET_FILE_DIR:RiceGroupTests>
        COMMAND_EXPAND_LISTS)
endif()

if(BUILD_TESTING)
    add_executable(RiceLightRenderTests Tests/LightRenderTests.cpp)
    target_link_libraries(RiceLightRenderTests PRIVATE RiceCore RiceBuildOptions glm::glm glfw glad::glad)
    add_test(NAME LightRendering COMMAND RiceLightRenderTests)
    add_custom_command(TARGET RiceLightRenderTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceLightRenderTests> $<TARGET_FILE_DIR:RiceLightRenderTests>
        COMMAND_EXPAND_LISTS)
endif()

if(BUILD_TESTING)
    add_executable(RiceShapeEditorTests Tests/ShapeEditorTests.cpp)
    target_link_libraries(RiceShapeEditorTests PRIVATE RiceCore RiceBuildOptions glm::glm glfw glad::glad)
    add_test(NAME ShapeEditor COMMAND RiceShapeEditorTests "${CMAKE_BINARY_DIR}/TestData/ShapeEditor.bmp")
    add_custom_command(TARGET RiceShapeEditorTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceShapeEditorTests> $<TARGET_FILE_DIR:RiceShapeEditorTests>
        COMMAND_EXPAND_LISTS)
endif()

if(BUILD_TESTING)
    add_executable(RicePivotAnchorTests Tests/PivotAnchorTests.cpp)
    target_link_libraries(RicePivotAnchorTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME PivotAnchor COMMAND RicePivotAnchorTests)
    add_custom_command(TARGET RicePivotAnchorTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RicePivotAnchorTests> $<TARGET_FILE_DIR:RicePivotAnchorTests>
        COMMAND_EXPAND_LISTS)
endif()

if(BUILD_TESTING)
    add_executable(RiceCameraTests Tests/CameraTests.cpp)
    target_link_libraries(RiceCameraTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME CameraComponents COMMAND RiceCameraTests)
    add_custom_command(TARGET RiceCameraTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceCameraTests> $<TARGET_FILE_DIR:RiceCameraTests>
        COMMAND_EXPAND_LISTS)
endif()

if(BUILD_TESTING)
    add_executable(RiceInspectorTests Tests/InspectorTests.cpp)
    target_link_libraries(RiceInspectorTests PRIVATE RiceCore RiceBuildOptions glm::glm imgui::imgui)
    add_test(NAME InspectorComponentActions COMMAND RiceInspectorTests "${CMAKE_BINARY_DIR}/TestData/Inspector")
    add_custom_command(TARGET RiceInspectorTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceInspectorTests> $<TARGET_FILE_DIR:RiceInspectorTests>
        COMMAND_EXPAND_LISTS)
endif()

if(BUILD_TESTING)
    add_executable(RiceEditorHistoryTests Tests/EditorHistoryTests.cpp)
    target_link_libraries(RiceEditorHistoryTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME EditorHistory COMMAND RiceEditorHistoryTests)
    add_custom_command(TARGET RiceEditorHistoryTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceEditorHistoryTests> $<TARGET_FILE_DIR:RiceEditorHistoryTests>
        COMMAND_EXPAND_LISTS)
endif()

if(BUILD_TESTING)
    add_executable(RiceHierarchyTests Tests/HierarchyTests.cpp)
    target_link_libraries(RiceHierarchyTests PRIVATE RiceCore RiceBuildOptions glm::glm imgui::imgui)
    add_test(NAME CachedHierarchy COMMAND RiceHierarchyTests "${CMAKE_BINARY_DIR}/TestData/Hierarchy")
    add_custom_command(TARGET RiceHierarchyTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceHierarchyTests> $<TARGET_FILE_DIR:RiceHierarchyTests>
        COMMAND_EXPAND_LISTS)
endif()
