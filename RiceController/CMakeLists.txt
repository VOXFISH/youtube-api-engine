find_package(FFMPEG REQUIRED)
# vcpkg's FindFFMPEG supplies import-library paths, not DLL targets. Describe
# the shared libraries explicitly so app-local copy and install include them.
get_filename_component(_rice_ffmpeg_prefix "${FFMPEG_libavcodec_INCLUDE_DIRS}" DIRECTORY)
set(_rice_ffmpeg_targets)
foreach(_component avcodec avformat avutil swresample swscale)
    file(GLOB _release_dll "${_rice_ffmpeg_prefix}/bin/${_component}-*.dll")
    file(GLOB _debug_dll "${_rice_ffmpeg_prefix}/debug/bin/${_component}-*.dll")
    list(LENGTH _release_dll _release_count)
    list(LENGTH _debug_dll _debug_count)
    if(NOT _release_count EQUAL 1 OR NOT _debug_count EQUAL 1)
        message(FATAL_ERROR "Rice requires one shared FFmpeg ${_component} DLL per configuration (x64-windows triplet)")
    endif()
    add_library(RiceFFmpeg_${_component} SHARED IMPORTED)
    set_target_properties(RiceFFmpeg_${_component} PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_RELEASE "${_release_dll}"
        IMPORTED_LOCATION_DEBUG "${_debug_dll}"
        IMPORTED_IMPLIB_RELEASE "${FFMPEG_lib${_component}_LIBRARY_RELEASE}"
        IMPORTED_IMPLIB_DEBUG "${FFMPEG_lib${_component}_LIBRARY_DEBUG}"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
        MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE)
    list(APPEND _rice_ffmpeg_targets RiceFFmpeg_${_component})
endforeach()
add_library(RiceMedia STATIC Source/Streaming/FfmpegVideoEncoder.cpp Source/Streaming/FfmpegOutput.cpp Source/Streaming/EncoderSessions.cpp Source/Streaming/WaitingImage.cpp Source/Streaming/PlayerOutput.cpp)
target_include_directories(RiceMedia PUBLIC Source PRIVATE ${FFMPEG_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR})
target_link_directories(RiceMedia PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(RiceMedia PUBLIC RiceBuildOptions PRIVATE ${_rice_ffmpeg_targets})

add_library(RiceControllerBackend STATIC Source/ControllerModel.cpp Source/ProcessManager.cpp
    Source/Streaming/SlotModel.cpp Source/Streaming/ApiKeyPool.cpp
    Source/Streaming/YouTubeClient.cpp Source/Streaming/CredentialStore.cpp Source/Streaming/PermissionPolicy.cpp
    Source/Streaming/GenericDataEditor.cpp Source/Streaming/ReplacementLifecycle.cpp Source/Streaming/GameReplacementFlow.cpp Source/Streaming/SlotChatSession.cpp)
target_include_directories(RiceControllerBackend PUBLIC Source Shared)
target_compile_definitions(RiceControllerBackend PUBLIC NOMINMAX)
target_link_libraries(RiceControllerBackend PUBLIC RiceBuildOptions RiceData RiceStreaming RiceMedia advapi32 ole32 shell32 user32 winhttp crypt32)

add_executable(RiceController WIN32 Source/Main.cpp Source/Streaming/StreamingPanel.cpp)
if(MSVC)
    target_link_options(RiceController PRIVATE /ENTRY:mainCRTStartup)
endif()
target_link_libraries(RiceController PRIVATE RiceControllerBackend imgui::imgui glad::glad glfw)
add_custom_command(TARGET RiceController POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RiceController> $<TARGET_FILE_DIR:RiceController>
    COMMAND_EXPAND_LISTS)
if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    add_custom_command(TARGET RiceController POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} $<TARGET_FILE_DIR:RiceController>
        COMMAND_EXPAND_LISTS)
endif()
install(TARGETS RiceController RUNTIME DESTINATION .)
install(FILES $<TARGET_RUNTIME_DLLS:RiceController> DESTINATION .)
install(FILES "${_rice_ffmpeg_prefix}/share/ffmpeg/copyright"
    DESTINATION ThirdParty/FFmpeg RENAME LICENSE.txt)
install(FILES "${PROJECT_SOURCE_DIR}/docs/requirements/FFmpegBuild.md"
    DESTINATION ThirdParty/FFmpeg RENAME BUILD-NOTES.md)

if(BUILD_TESTING)
    add_executable(RiceSlotChatSessionTests Tests/SlotChatSessionTests.cpp)
    target_link_libraries(RiceSlotChatSessionTests PRIVATE RiceControllerBackend)
    add_test(NAME SlotChatSession COMMAND RiceSlotChatSessionTests)
    set_tests_properties(SlotChatSession PROPERTIES TIMEOUT 30)
    add_executable(RiceReplacementLifecycleTests Tests/ReplacementLifecycleTests.cpp)
    target_link_libraries(RiceReplacementLifecycleTests PRIVATE RiceControllerBackend)
    add_test(NAME ReplacementLifecycle COMMAND RiceReplacementLifecycleTests)
    add_executable(RiceEncoderSessionTests Tests/EncoderSessionTests.cpp)
    target_link_libraries(RiceEncoderSessionTests PRIVATE RiceControllerBackend)
    add_test(NAME EncoderSessions COMMAND RiceEncoderSessionTests)
    add_executable(RiceWaitingImageTests Tests/WaitingImageTests.cpp)
    target_link_libraries(RiceWaitingImageTests PRIVATE RiceControllerBackend)
    add_test(NAME WaitingImage COMMAND RiceWaitingImageTests "${CMAKE_BINARY_DIR}/TestData/WaitingImage")
    # Explicit opt-in: this test consumes real GPU encoding resources.
    option(RICE_TEST_HARDWARE_ENCODER "Run local NVENC encode/decode tests" OFF)
    add_executable(RiceHardwareEncoderTests Tests/HardwareEncoderTests.cpp)
    target_include_directories(RiceHardwareEncoderTests PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(RiceHardwareEncoderTests PRIVATE RiceControllerBackend)
    if(RICE_TEST_HARDWARE_ENCODER)
        add_test(NAME HardwareEncoder COMMAND RiceHardwareEncoderTests "${CMAKE_BINARY_DIR}/TestData/HardwareEncoder")
        set_tests_properties(HardwareEncoder PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
    endif()
    add_executable(RiceGenericDataEditorTests Tests/GenericDataEditorTests.cpp)
    target_link_libraries(RiceGenericDataEditorTests PRIVATE RiceControllerBackend)
    add_test(NAME GenericDataEditor COMMAND RiceGenericDataEditorTests)
    add_executable(RiceYouTubeClientTests Tests/YouTubeClientTests.cpp)
    target_link_libraries(RiceYouTubeClientTests PRIVATE RiceControllerBackend)
    add_test(NAME YouTubeClient COMMAND RiceYouTubeClientTests "${CMAKE_BINARY_DIR}/TestData/YouTubeClient")
    add_executable(RiceStreamingSlotTests Tests/StreamingSlotTests.cpp)
    target_link_libraries(RiceStreamingSlotTests PRIVATE RiceControllerBackend)
    add_test(NAME StreamingSlots COMMAND RiceStreamingSlotTests)
    add_executable(RiceStreamingPanelTests Tests/StreamingPanelTests.cpp Source/Streaming/StreamingPanel.cpp)
    target_link_libraries(RiceStreamingPanelTests PRIVATE RiceControllerBackend imgui::imgui)
    add_test(NAME StreamingPanel COMMAND RiceStreamingPanelTests "${CMAKE_BINARY_DIR}/TestData/StreamingPanel")
    add_executable(RiceControllerModelTests Tests/ModelTests.cpp)
    target_link_libraries(RiceControllerModelTests PRIVATE RiceControllerBackend)
    add_test(NAME ControllerModel COMMAND RiceControllerModelTests "${CMAKE_BINARY_DIR}/TestData/ControllerModel")
    add_test(NAME ControllerWindow COMMAND RiceController --smoke --config "${CMAKE_BINARY_DIR}/TestData/ControllerWindow/controller.json")
    set_tests_properties(ControllerWindow PROPERTIES TIMEOUT 45)
    if(TARGET RicePlayer)
        add_executable(RiceControllerIntegrationTests Tests/IntegrationTests.cpp)
        target_link_libraries(RiceControllerIntegrationTests PRIVATE RiceControllerBackend)
        add_test(NAME ControllerIntegration COMMAND RiceControllerIntegrationTests $<TARGET_FILE:RicePlayer>
            "${PROJECT_SOURCE_DIR}/Presentation/Grass1000" "${CMAKE_BINARY_DIR}/TestData/ControllerIntegration")
        set_tests_properties(ControllerIntegration PROPERTIES TIMEOUT 180)
        if(RICE_TEST_HARDWARE_ENCODER)
            add_test(NAME PlayerMediaIntegration COMMAND RiceControllerIntegrationTests $<TARGET_FILE:RicePlayer>
                "${PROJECT_SOURCE_DIR}/Presentation/Grass1000" "${CMAKE_BINARY_DIR}/TestData/PlayerMediaIntegration" --media)
            set_tests_properties(PlayerMediaIntegration PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        endif()
    endif()
endif()
