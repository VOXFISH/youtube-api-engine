target_sources(RiceCore PRIVATE NativeScriptCompiler.cpp ScriptHost.cpp ScriptAuthoring.cpp)
target_link_libraries(RiceCore PUBLIC RiceData)
# Source-file properties need to be visible in the scope owning RiceCore.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/NativeScriptCompiler.cpp" TARGET_DIRECTORY RiceCore
    PROPERTY COMPILE_DEFINITIONS "RICE_SCRIPT_CMAKE=L\"${CMAKE_COMMAND}\";RICE_SCRIPT_MODULE_PROJECT=L\"${CMAKE_CURRENT_SOURCE_DIR}/ModuleProject\"")
if(TARGET RiceSandbox)
    add_custom_target(RiceScriptSDK
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RiceSandbox>/ScriptSDK/Scripting"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/ScriptRuntime.h" "${CMAKE_CURRENT_SOURCE_DIR}/ScriptRuntimeValue.h" "$<TARGET_FILE_DIR:RiceSandbox>/ScriptSDK/Scripting/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/ModuleProject" "$<TARGET_FILE_DIR:RiceSandbox>/ScriptSDK/Scripting/ModuleProject"
        VERBATIM)
    add_dependencies(RiceSandbox RiceScriptSDK)
endif()
install(FILES ScriptRuntime.h ScriptRuntimeValue.h DESTINATION Engine/Runtime/ScriptSDK/Scripting)
install(DIRECTORY ModuleProject DESTINATION Engine/Runtime/ScriptSDK/Scripting)
if(BUILD_TESTING)
    add_executable(RiceScriptAuthoringTests "${PROJECT_SOURCE_DIR}/Tests/ScriptAuthoringTests.cpp")
    target_link_libraries(RiceScriptAuthoringTests PRIVATE RiceCore RiceBuildOptions)
    add_test(NAME ScriptAuthoring COMMAND RiceScriptAuthoringTests)
    add_executable(RiceScriptRuntimeTests "${PROJECT_SOURCE_DIR}/Tests/ScriptRuntimeTests.cpp")
    target_link_libraries(RiceScriptRuntimeTests PRIVATE RiceCore RiceBuildOptions)
    add_test(NAME ScriptRuntime COMMAND RiceScriptRuntimeTests)
    add_executable(RiceScriptPlayTests "${PROJECT_SOURCE_DIR}/Tests/ScriptPlayTests.cpp")
    target_link_libraries(RiceScriptPlayTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME ScriptNativeSaveAndPlay COMMAND RiceScriptPlayTests "${CMAKE_BINARY_DIR}/TestData/Scripting")
    if(TARGET RiceScriptSDK)
        add_dependencies(RiceScriptPlayTests RiceScriptSDK)
    endif()
    add_executable(RiceScriptEditorTests "${PROJECT_SOURCE_DIR}/Tests/ScriptEditorTests.cpp")
    target_link_libraries(RiceScriptEditorTests PRIVATE RiceCore RiceBuildOptions glm::glm imgui::imgui)
    add_test(NAME ScriptEditor COMMAND RiceScriptEditorTests "${CMAKE_BINARY_DIR}/TestData/ScriptEditor")
    add_executable(RiceScriptChildTests "${PROJECT_SOURCE_DIR}/Tests/ScriptChildTests.cpp")
    target_link_libraries(RiceScriptChildTests PRIVATE RiceCore RiceBuildOptions glm::glm)
    add_test(NAME ScriptChildReferences COMMAND RiceScriptChildTests "${CMAKE_BINARY_DIR}/TestData/ScriptChildren")
    if(TARGET RiceScriptSDK)
        add_dependencies(RiceScriptChildTests RiceScriptSDK)
    endif()
    foreach(target RiceScriptPlayTests RiceScriptEditorTests RiceScriptChildTests)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS)
    endforeach()
endif()
