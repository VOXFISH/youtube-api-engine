add_library(RiceCore SHARED)

target_sources(
    RiceCore
    PRIVATE
        Source/Application.cpp
        Source/Debug.cpp
        Include/ImGui/ImGuiLayer.cpp
        Source/Layer.cpp
        Source/LayerStack.cpp
        Source/PreCompiledheaders.cpp
        Source/Time.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS Include
        FILES
            Include/Rice.h
            Include/Event/ApplicationEvent.h
            Include/Event/Event.h
            Include/Event/KeyCode.h
            Include/Event/KeyEvent.h
            Include/Event/MouseCode.h
            Include/Event/MouseEvent.h
            Include/Rice/Application.h
            Include/Rice/Core.h
            Include/Rice/Debug.h
            Include/Rice/EntryPoint.h
            Include/Rice/PreCompiledHeaders.h
            Include/Rice/Time.h
            Include/Rice/Window.h
            Include/Rice/Layer.h
            Include/Rice/LayerStack.h
            Include/ImGui/ImGuiLayer.h
)

target_include_directories(
    RiceCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Include
        ${PROJECT_SOURCE_DIR}/Engine
)

target_compile_definitions(
    RiceCore
    PRIVATE
        RICE_BUILD_DLL
)

set_target_properties(RiceCore PROPERTIES
    PREFIX ""
    OUTPUT_NAME "RiceCore"
)

target_link_libraries(
    RiceCore
    PUBLIC
        spdlog::spdlog
        imgui::imgui
    PRIVATE
        RiceBuildOptions
        glad::glad
        glfw
        glm::glm
)
