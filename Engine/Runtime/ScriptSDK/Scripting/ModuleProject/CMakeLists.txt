cmake_minimum_required(VERSION 3.25)
project(RiceUserScript LANGUAGES CXX)
add_library(script MODULE "${CMAKE_BINARY_DIR}/candidate.cpp" VerifyEntry.cpp)
target_compile_features(script PRIVATE cxx_std_20)
target_include_directories(script PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." "${CMAKE_CURRENT_SOURCE_DIR}/.." "${RICE_SOURCE_DIRECTORY}")
if(MSVC)
    target_compile_options(script PRIVATE /utf-8 /EHsc /diagnostics:column)
endif()
set_target_properties(script PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/$<0:>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/$<0:>")
