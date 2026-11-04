target_sources(
    RiceCore
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Windows/WindowsWindow.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Windows/WindowsWindow.h
        ${CMAKE_CURRENT_SOURCE_DIR}/Windows/WindowsInput.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Windows/WindowsInput.h
        ${CMAKE_CURRENT_SOURCE_DIR}/Windows/WindowsFileDialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/FileDialog.h
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenGL/OpenGLShader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenGL/OpenGLShader.h
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenGL/OpenGLTexture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenGL/OpenGLTexture.h
)
target_link_libraries(RiceCore PRIVATE comdlg32 shell32)
