foreach(app RiceSandbox RicePlayer)
    add_executable(${app} WIN32 Source/SandboxApp.cpp)
    if(MSVC)
        target_link_options(${app} PRIVATE /ENTRY:mainCRTStartup)
    endif()
    target_link_libraries(${app} PRIVATE RiceBuildOptions RiceCore glm::glm)
    target_compile_definitions(${app} PRIVATE NOMINMAX)
    if(app STREQUAL "RicePlayer")
        target_compile_definitions(${app} PRIVATE RICE_PLAYER)
    else()
        set_target_properties(${app} PROPERTIES OUTPUT_NAME "RiceEditor")
    endif()
    add_custom_command(TARGET ${app} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${app}> $<TARGET_FILE_DIR:${app}>
        COMMAND_EXPAND_LISTS)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET ${app} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} $<TARGET_FILE_DIR:${app}>
            COMMAND_EXPAND_LISTS)
    endif()
    install(TARGETS ${app} RUNTIME DESTINATION .)
    install(FILES $<TARGET_RUNTIME_DLLS:${app}> DESTINATION .)
endforeach()
