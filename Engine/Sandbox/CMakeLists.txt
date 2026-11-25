foreach(app RiceSandbox RicePlayer)
    add_executable(${app} WIN32 Source/SandboxApp.cpp)
    rice_set_app_icon(${app} Rice)
    if(MSVC)
        target_link_options(${app} PRIVATE /ENTRY:mainCRTStartup)
    endif()
    target_link_libraries(${app} PRIVATE RiceBuildOptions RiceCore RiceData glm::glm)
    target_compile_definitions(${app} PRIVATE NOMINMAX)
    if(app STREQUAL "RicePlayer")
        target_compile_definitions(${app} PRIVATE RICE_PLAYER)
        target_link_libraries(${app} PRIVATE advapi32 RiceData)
        string(JOIN "|" playerSystemDlls ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
        add_custom_command(TARGET ${app} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/Tools/RiceCon/RicePlayer.control" $<TARGET_FILE_DIR:${app}>
            COMMAND ${CMAKE_COMMAND} "-DDLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:${app}>,|>|${playerSystemDlls}"
                "-DOUTPUT=$<TARGET_FILE_DIR:${app}>/RicePlayer.dlls" -P "${PROJECT_SOURCE_DIR}/scripts/WritePlayerDlls.cmake"
            VERBATIM)
        install(FILES "$<TARGET_FILE_DIR:${app}>/RicePlayer.dlls" DESTINATION Engine/Runtime)
        install(FILES "${PROJECT_SOURCE_DIR}/Tools/RiceCon/RicePlayer.control" DESTINATION Engine/Runtime)
    else()
        set_target_properties(${app} PROPERTIES OUTPUT_NAME "Rice")
        rice_use_ui_font(${app})
        if(MSVC)
            target_compile_options(${app} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/bigobj>")
        endif()
    endif()
    add_custom_command(TARGET ${app} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${app}> $<TARGET_FILE_DIR:${app}>
        COMMAND_EXPAND_LISTS)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET ${app} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} $<TARGET_FILE_DIR:${app}>
            COMMAND_EXPAND_LISTS)
    endif()
    install(TARGETS ${app} RUNTIME DESTINATION Engine/Runtime)
    install(FILES $<TARGET_RUNTIME_DLLS:${app}> DESTINATION Engine/Runtime)
endforeach()

# Reproducible editor scene and actual-engine preview for the graphics lecture.
add_executable(RiceShadowPresentation EXCLUDE_FROM_ALL Tools/BuildShadowPresentation.cpp)
target_link_libraries(RiceShadowPresentation PRIVATE RiceBuildOptions RiceCore glm::glm glfw glad::glad)
add_custom_command(TARGET RiceShadowPresentation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceShadowPresentation> $<TARGET_FILE_DIR:RiceShadowPresentation>
    COMMAND_EXPAND_LISTS)

# Offline recording uses the same saved scene, renderer and live telemetry.
find_package(FFMPEG REQUIRED)
add_executable(RiceShadowRecording EXCLUDE_FROM_ALL Tools/RecordShadowPresentation.cpp)
target_include_directories(RiceShadowRecording PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(RiceShadowRecording PRIVATE RiceBuildOptions RiceCore RiceControllerBackend glm::glm glfw glad::glad imgui::imgui)
add_custom_command(TARGET RiceShadowRecording POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceShadowRecording> $<TARGET_FILE_DIR:RiceShadowRecording>
    COMMAND_EXPAND_LISTS)
