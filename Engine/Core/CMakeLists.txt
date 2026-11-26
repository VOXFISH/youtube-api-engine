add_library(RiceCore STATIC)

# ufbx v0.23.1 (26a482ae66871d7de36eb722aa060bce95bce274).
add_library(RiceUfbx STATIC ${PROJECT_SOURCE_DIR}/Dependencies/ufbx/ufbx.c)
target_include_directories(RiceUfbx PUBLIC ${PROJECT_SOURCE_DIR}/Dependencies/ufbx)
install(FILES ${PROJECT_SOURCE_DIR}/Dependencies/ufbx/LICENSE DESTINATION Engine/ThirdParty/ufbx RENAME LICENSE.txt)

target_sources(
    RiceCore
    PRIVATE
        Source/Application.cpp
        Source/Benchmark.cpp
        Source/Debug.cpp
        Include/ImGui/ImGuiLayer.cpp
        Source/Layer.cpp
        Source/LayerStack.cpp
        Source/PreCompiledheaders.cpp
        Source/Time.cpp
        ${PROJECT_SOURCE_DIR}/Dependencies/stb_image/StbImage.cpp
        ${PROJECT_SOURCE_DIR}/Project/FbxModel.cpp
        ${PROJECT_SOURCE_DIR}/Project/FbxImport.cpp
        ${PROJECT_SOURCE_DIR}/Project/BuiltinSkyboxes.cpp
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
            Include/Rice/Benchmark.h
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
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/Dependencies/stb_image
)

target_compile_definitions(
    RiceCore
    PUBLIC
        RICE_STATIC
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
        RiceUfbx
        glad::glad
        glfw
        glm::glm
)
