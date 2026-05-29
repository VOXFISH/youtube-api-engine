add_executable(RiceSandbox
    Source/SandboxApp.cpp
)

target_link_libraries(RiceSandbox
    PRIVATE
        RiceCore
)

target_compile_definitions(RiceSandbox
    PRIVATE
        RICE_ENABLE_ENTRY_POINT
)

add_custom_command(TARGET RiceSandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:RiceCore>
        $<TARGET_RUNTIME_DLLS:RiceSandbox>
        $<TARGET_FILE_DIR:RiceSandbox>
    COMMAND_EXPAND_LISTS
)
