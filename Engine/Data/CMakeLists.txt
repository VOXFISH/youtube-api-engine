add_library(RiceData STATIC SqliteStore.cpp)
target_include_directories(RiceData PUBLIC ${PROJECT_SOURCE_DIR}/..)
target_link_libraries(RiceData PUBLIC RiceBuildOptions glm::glm)

if(BUILD_TESTING)
    add_executable(RiceDataSealTests ${PROJECT_SOURCE_DIR}/Tests/DataSealTests.cpp)
    target_link_libraries(RiceDataSealTests PRIVATE RiceData)
    add_test(NAME DataSealWrite COMMAND RiceDataSealTests "${CMAKE_BINARY_DIR}/TestData/DataSeal" write)
    add_test(NAME DataSealRead COMMAND RiceDataSealTests "${CMAKE_BINARY_DIR}/TestData/DataSeal" read)
    set_tests_properties(DataSealWrite PROPERTIES FIXTURES_SETUP RetirementDatabase)
    set_tests_properties(DataSealRead PROPERTIES FIXTURES_REQUIRED RetirementDatabase)
    add_executable(RiceDataRoutingTests ${PROJECT_SOURCE_DIR}/Tests/DataRoutingTests.cpp)
    target_link_libraries(RiceDataRoutingTests PRIVATE RiceData)
    add_test(NAME DataRouting COMMAND RiceDataRoutingTests "${CMAKE_BINARY_DIR}/TestData/DataRouting")
    add_executable(RiceDataContextTests ${PROJECT_SOURCE_DIR}/Tests/DataContextTests.cpp)
    target_link_libraries(RiceDataContextTests PRIVATE RiceData)
    add_test(NAME DataContext COMMAND RiceDataContextTests "${CMAKE_BINARY_DIR}/TestData/DataContext")
    add_library(RiceInvalidGlobalTemporary OBJECT EXCLUDE_FROM_ALL
        ${PROJECT_SOURCE_DIR}/Tests/CompileFail/GlobalTemporary.cpp)
    target_link_libraries(RiceInvalidGlobalTemporary PRIVATE RiceData)
    add_test(NAME DataRoutingCompileContract COMMAND ${CMAKE_COMMAND}
        "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" "-DTEST_CONFIG=$<CONFIG>"
        "-DTEST_TARGET=RiceInvalidGlobalTemporary"
        "-DEXPECTED_DIAGNOSTIC=global variables can't be game(stream) temporary."
        -P "${PROJECT_SOURCE_DIR}/scripts/CheckExpectedBuildFailure.cmake")
    set_tests_properties(DataRoutingCompileContract PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
endif()
