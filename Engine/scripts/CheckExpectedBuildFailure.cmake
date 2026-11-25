if(NOT DEFINED BUILD_DIRECTORY OR NOT DEFINED TEST_TARGET OR NOT DEFINED EXPECTED_DIAGNOSTIC)
    message(FATAL_ERROR "Expected-failure test arguments are missing")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIRECTORY}" --config "${TEST_CONFIG}" --target "${TEST_TARGET}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 90)
if(result EQUAL 0)
    message(FATAL_ERROR "Invalid data declaration unexpectedly compiled")
endif()
string(FIND "${output}\n${errors}" "${EXPECTED_DIAGNOSTIC}" position)
if(position EQUAL -1)
    message(FATAL_ERROR "Build failed without the expected routing diagnostic:\n${output}\n${errors}")
endif()
message(STATUS "Invalid routing declaration produced the required diagnostic")
