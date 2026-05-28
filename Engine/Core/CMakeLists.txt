add_library(RiceCore SHARED) # 동적 라이브러리로 선언 : 여러 개의 실행파일에서 한 번에 참고할 수 있음.

target_sources(
    RiceCore
    PRIVATE
        Source/Application.cpp
        Source/Debug.cpp
        Source/Time.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS Include
        FILES
        include/Rice/Application.h
        include/Rice/EntryPoint.h
        include/Rice/Time.h
)

target_include_directories(
    RiceCore
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

target_compile_definitions(
    RiceCore
    PRIVATE RICE_BUILD_DLL
)

set_target_properties(RiceCore PROPERTIES
    PREFIX ""
    OUTPUT_NAME "RiceCore"
)

find_package(spdlog CONFIG REQUIRED)

target_link_libraries(
    RiceCore
    PUBLIC
        spdlog::spdlog
)

#[[ find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

target_link_libraries(
    RiceCore
    PUBLIC
        glfw
        glm::glm
        glad::glad
) ]]