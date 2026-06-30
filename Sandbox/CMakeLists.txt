add_executable(RiceSandbox
    Assets/SandboxCube.cpp
    Source/SandboxApp.cpp
)

target_link_libraries(RiceSandbox
    PRIVATE
        RiceBuildOptions
        RiceCore
        glm::glm
)

target_compile_definitions(RiceSandbox
    PRIVATE
        RICE_ENABLE_ENTRY_POINT
)

add_custom_command(TARGET RiceSandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:RiceSandbox>
        $<TARGET_FILE_DIR:RiceSandbox>
    COMMAND_EXPAND_LISTS
)
