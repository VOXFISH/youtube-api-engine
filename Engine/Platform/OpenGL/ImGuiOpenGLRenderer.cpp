#include "ImGuiOpenGLRenderer.h"

#include "Event/ApplicationEvent.h"
#include "Event/KeyCode.h"
#include "Event/KeyEvent.h"
#include "Event/MouseCode.h"
#include "Event/MouseEvent.h"
#include "Rice/Debug.h"

#include <imgui.h>

#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <cstddef>

namespace Rice
{
    namespace
    {
        const char* GetDefaultGlslVersion()
        {
            const char* versionString = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            if (versionString == nullptr)
            {
                return "#version 130";
            }

            GLint majorVersion = 0;
            GLint minorVersion = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
            glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

            if (majorVersion >= 4)
            {
                return "#version 410";
            }

            if (majorVersion == 3 && minorVersion >= 2)
            {
                return "#version 150";
            }

            return "#version 130";
        }

        ImGuiKey ImGuiKeyFromRiceKey(KeyCode keyCode)
        {
            switch (keyCode)
            {
            case Key::Tab: return ImGuiKey_Tab;
            case Key::Left: return ImGuiKey_LeftArrow;
            case Key::Right: return ImGuiKey_RightArrow;
            case Key::Up: return ImGuiKey_UpArrow;
            case Key::Down: return ImGuiKey_DownArrow;
            case Key::PageUp: return ImGuiKey_PageUp;
            case Key::PageDown: return ImGuiKey_PageDown;
            case Key::Home: return ImGuiKey_Home;
            case Key::End: return ImGuiKey_End;
            case Key::Insert: return ImGuiKey_Insert;
            case Key::Delete: return ImGuiKey_Delete;
            case Key::Backspace: return ImGuiKey_Backspace;
            case Key::Space: return ImGuiKey_Space;
            case Key::Enter: return ImGuiKey_Enter;
            case Key::Esc: return ImGuiKey_Escape;
            case Key::Apostrophe: return ImGuiKey_Apostrophe;
            case Key::Comma: return ImGuiKey_Comma;
            case Key::Minus: return ImGuiKey_Minus;
            case Key::Period: return ImGuiKey_Period;
            case Key::Slash: return ImGuiKey_Slash;
            case Key::D0: return ImGuiKey_0;
            case Key::D1: return ImGuiKey_1;
            case Key::D2: return ImGuiKey_2;
            case Key::D3: return ImGuiKey_3;
            case Key::D4: return ImGuiKey_4;
            case Key::D5: return ImGuiKey_5;
            case Key::D6: return ImGuiKey_6;
            case Key::D7: return ImGuiKey_7;
            case Key::D8: return ImGuiKey_8;
            case Key::D9: return ImGuiKey_9;
            case Key::Semicolon: return ImGuiKey_Semicolon;
            case Key::Equal: return ImGuiKey_Equal;
            case Key::LeftBracket: return ImGuiKey_LeftBracket;
            case Key::BackSlash: return ImGuiKey_Backslash;
            case Key::RightBracket: return ImGuiKey_RightBracket;
            case Key::A: return ImGuiKey_A;
            case Key::B: return ImGuiKey_B;
            case Key::C: return ImGuiKey_C;
            case Key::D: return ImGuiKey_D;
            case Key::E: return ImGuiKey_E;
            case Key::F: return ImGuiKey_F;
            case Key::G: return ImGuiKey_G;
            case Key::H: return ImGuiKey_H;
            case Key::I: return ImGuiKey_I;
            case Key::J: return ImGuiKey_J;
            case Key::K: return ImGuiKey_K;
            case Key::L: return ImGuiKey_L;
            case Key::M: return ImGuiKey_M;
            case Key::N: return ImGuiKey_N;
            case Key::O: return ImGuiKey_O;
            case Key::P: return ImGuiKey_P;
            case Key::Q: return ImGuiKey_Q;
            case Key::R: return ImGuiKey_R;
            case Key::S: return ImGuiKey_S;
            case Key::T: return ImGuiKey_T;
            case Key::U: return ImGuiKey_U;
            case Key::V: return ImGuiKey_V;
            case Key::W: return ImGuiKey_W;
            case Key::X: return ImGuiKey_X;
            case Key::Y: return ImGuiKey_Y;
            case Key::Z: return ImGuiKey_Z;
            case Key::F1: return ImGuiKey_F1;
            case Key::F2: return ImGuiKey_F2;
            case Key::F3: return ImGuiKey_F3;
            case Key::F4: return ImGuiKey_F4;
            case Key::F5: return ImGuiKey_F5;
            case Key::F6: return ImGuiKey_F6;
            case Key::F7: return ImGuiKey_F7;
            case Key::F8: return ImGuiKey_F8;
            case Key::F9: return ImGuiKey_F9;
            case Key::F10: return ImGuiKey_F10;
            case Key::F11: return ImGuiKey_F11;
            case Key::F12: return ImGuiKey_F12;
            case Key::LeftShift: return ImGuiKey_LeftShift;
            case Key::LeftControl: return ImGuiKey_LeftCtrl;
            case Key::LeftAlt: return ImGuiKey_LeftAlt;
            case Key::LeftSuper: return ImGuiKey_LeftSuper;
            case Key::RightShift: return ImGuiKey_RightShift;
            case Key::RightControl: return ImGuiKey_RightCtrl;
            case Key::RightAlt: return ImGuiKey_RightAlt;
            case Key::RightSuper: return ImGuiKey_RightSuper;
            case Key::Menu: return ImGuiKey_Menu;
            case Key::KP0: return ImGuiKey_Keypad0;
            case Key::KP1: return ImGuiKey_Keypad1;
            case Key::KP2: return ImGuiKey_Keypad2;
            case Key::KP3: return ImGuiKey_Keypad3;
            case Key::KP4: return ImGuiKey_Keypad4;
            case Key::KP5: return ImGuiKey_Keypad5;
            case Key::KP6: return ImGuiKey_Keypad6;
            case Key::KP7: return ImGuiKey_Keypad7;
            case Key::KP8: return ImGuiKey_Keypad8;
            case Key::KP9: return ImGuiKey_Keypad9;
            case Key::KPDecimal: return ImGuiKey_KeypadDecimal;
            case Key::KPDivide: return ImGuiKey_KeypadDivide;
            case Key::KPMultiply: return ImGuiKey_KeypadMultiply;
            case Key::KPSubtract: return ImGuiKey_KeypadSubtract;
            case Key::KPAdd: return ImGuiKey_KeypadAdd;
            case Key::KPEnter: return ImGuiKey_KeypadEnter;
            case Key::KPEqual: return ImGuiKey_KeypadEqual;
            default: return ImGuiKey_None;
            }
        }

        int ImGuiMouseButtonFromRiceMouse(MouseCode button)
        {
            switch (button)
            {
            case Mouse::ButtonLeft: return 0;
            case Mouse::ButtonRight: return 1;
            case Mouse::ButtonMiddle: return 2;
            default: return static_cast<int>(button);
            }
        }

        void UpdateKeyModifiers(ImGuiIO& io, GLFWwindow* window)
        {
            io.AddKeyEvent(ImGuiMod_Ctrl,
                glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
            io.AddKeyEvent(ImGuiMod_Shift,
                glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
            io.AddKeyEvent(ImGuiMod_Alt,
                glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
            io.AddKeyEvent(ImGuiMod_Super,
                glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS);
        }

        GLuint CreateShader(GLenum shaderType, const char* source)
        {
            GLuint shader = glCreateShader(shaderType);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);

            GLint compileStatus = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
            if (compileStatus == GL_FALSE)
            {
                GLint logLength = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
                std::string infoLog(static_cast<size_t>(logLength), '\0');
                glGetShaderInfoLog(shader, logLength, nullptr, infoLog.data());
                RICE_CORE_ERROR("ImGui shader compilation failed: {0}", infoLog);
            }

            return shader;
        }
    }

    void ImGuiOpenGLRenderer::Init(GLFWwindow* window, const char* glslVersion)
    {
        RICE_CORE_ASSERT(window != nullptr, "ImGuiOpenGLRenderer requires a valid GLFWwindow.");
        RICE_CORE_ASSERT(!m_Initialized, "ImGuiOpenGLRenderer already initialized.");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "RiceImGuiOpenGLRenderer";
        io.BackendPlatformName = "RiceEventBackend";
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        m_Window = window;
        m_GlslVersion = (glslVersion != nullptr) ? glslVersion : GetDefaultGlslVersion();
        m_Time = glfwGetTime();

        CreateDeviceObjects();
        CreateFontsTexture();
        m_Initialized = true;
    }

    void ImGuiOpenGLRenderer::Shutdown()
    {
        if (!m_Initialized)
        {
            return;
        }

        DestroyFontsTexture();
        DestroyDeviceObjects();
        ImGui::DestroyContext();

        m_Window = nullptr;
        m_Time = 0.0;
        m_Initialized = false;
    }

    void ImGuiOpenGLRenderer::BeginFrame()
    {
        RICE_CORE_ASSERT(m_Initialized, "ImGuiOpenGLRenderer must be initialized before BeginFrame.");

        if (m_FontTexture == 0)
        {
            CreateFontsTexture();
        }

        ImGuiIO& io = ImGui::GetIO();

        int windowWidth = 0;
        int windowHeight = 0;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);
        glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);

        io.DisplaySize = ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
        if (windowWidth > 0 && windowHeight > 0)
        {
            io.DisplayFramebufferScale = ImVec2(
                static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth),
                static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight));
        }

        const double currentTime = glfwGetTime();
        io.DeltaTime = (m_Time > 0.0) ? static_cast<float>(currentTime - m_Time) : (1.0f / 60.0f);
        m_Time = currentTime;

        ImGui::NewFrame();
    }

    void ImGuiOpenGLRenderer::EndFrame()
    {
        RICE_CORE_ASSERT(m_Initialized, "ImGuiOpenGLRenderer must be initialized before EndFrame.");

        ImGui::Render();
        RenderDrawData();
    }

    void ImGuiOpenGLRenderer::OnEvent(Event& event)
    {
        if (!m_Initialized)
        {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        EventDispatcher dispatcher(event);

        dispatcher.Dispatch<MouseMovedEvent>([&io](MouseMovedEvent& e)
        {
            io.AddMousePosEvent(e.GetX(), e.GetY());
            return io.WantCaptureMouse;
        });

        dispatcher.Dispatch<MouseScrolledEvent>([&io](MouseScrolledEvent& e)
        {
            io.AddMouseWheelEvent(e.GetXOffset(), e.GetYOffset());
            return io.WantCaptureMouse;
        });

        dispatcher.Dispatch<MouseButtonPressedEvent>([&io](MouseButtonPressedEvent& e)
        {
            io.AddMouseButtonEvent(ImGuiMouseButtonFromRiceMouse(e.GetMouseButton()), true);
            return io.WantCaptureMouse;
        });

        dispatcher.Dispatch<MouseButtonReleasedEvent>([&io](MouseButtonReleasedEvent& e)
        {
            io.AddMouseButtonEvent(ImGuiMouseButtonFromRiceMouse(e.GetMouseButton()), false);
            return io.WantCaptureMouse;
        });

        dispatcher.Dispatch<KeyPressedEvent>([this, &io](KeyPressedEvent& e)
        {
            UpdateKeyModifiers(io, m_Window);
            const ImGuiKey imguiKey = ImGuiKeyFromRiceKey(e.GetKeyCode());
            if (imguiKey != ImGuiKey_None)
            {
                io.AddKeyEvent(imguiKey, true);
            }
            return io.WantCaptureKeyboard;
        });

        dispatcher.Dispatch<KeyReleasedEvent>([this, &io](KeyReleasedEvent& e)
        {
            UpdateKeyModifiers(io, m_Window);
            const ImGuiKey imguiKey = ImGuiKeyFromRiceKey(e.GetKeyCode());
            if (imguiKey != ImGuiKey_None)
            {
                io.AddKeyEvent(imguiKey, false);
            }
            return io.WantCaptureKeyboard;
        });

        dispatcher.Dispatch<KeyTypedEvent>([&io](KeyTypedEvent& e)
        {
            io.AddInputCharacter(e.GetCodepoint());
            return io.WantCaptureKeyboard;
        });

        dispatcher.Dispatch<WindowResizeEvent>([&io](WindowResizeEvent& e)
        {
            io.DisplaySize = ImVec2(static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()));
            return false;
        });
    }

    bool ImGuiOpenGLRenderer::WantsCaptureMouse() const
    {
        return m_Initialized && ImGui::GetIO().WantCaptureMouse;
    }

    bool ImGuiOpenGLRenderer::WantsCaptureKeyboard() const
    {
        return m_Initialized && ImGui::GetIO().WantCaptureKeyboard;
    }

    void ImGuiOpenGLRenderer::CreateDeviceObjects()
    {
        const std::string vertexShaderSource =
            std::string(m_GlslVersion) + "\n"
            "uniform mat4 ProjMtx;\n"
            "layout (location = 0) in vec2 Position;\n"
            "layout (location = 1) in vec2 UV;\n"
            "layout (location = 2) in vec4 Color;\n"
            "out vec2 Frag_UV;\n"
            "out vec4 Frag_Color;\n"
            "void main()\n"
            "{\n"
            "    Frag_UV = UV;\n"
            "    Frag_Color = Color;\n"
            "    gl_Position = ProjMtx * vec4(Position.xy, 0, 1);\n"
            "}\n";

        const std::string fragmentShaderSource =
            std::string(m_GlslVersion) + "\n"
            "uniform sampler2D Texture;\n"
            "in vec2 Frag_UV;\n"
            "in vec4 Frag_Color;\n"
            "out vec4 Out_Color;\n"
            "void main()\n"
            "{\n"
            "    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
            "}\n";

        m_VertHandle = CreateShader(GL_VERTEX_SHADER, vertexShaderSource.c_str());
        m_FragHandle = CreateShader(GL_FRAGMENT_SHADER, fragmentShaderSource.c_str());

        m_ShaderHandle = glCreateProgram();
        glAttachShader(m_ShaderHandle, m_VertHandle);
        glAttachShader(m_ShaderHandle, m_FragHandle);
        glLinkProgram(m_ShaderHandle);

        GLint linkStatus = GL_FALSE;
        glGetProgramiv(m_ShaderHandle, GL_LINK_STATUS, &linkStatus);
        if (linkStatus == GL_FALSE)
        {
            GLint logLength = 0;
            glGetProgramiv(m_ShaderHandle, GL_INFO_LOG_LENGTH, &logLength);
            std::string infoLog(static_cast<size_t>(logLength), '\0');
            glGetProgramInfoLog(m_ShaderHandle, logLength, nullptr, infoLog.data());
            RICE_CORE_ERROR("ImGui shader link failed: {0}", infoLog);
        }

        m_AttribLocationTex = glGetUniformLocation(m_ShaderHandle, "Texture");
        m_AttribLocationProjMtx = glGetUniformLocation(m_ShaderHandle, "ProjMtx");
        m_AttribLocationVtxPos = 0;
        m_AttribLocationVtxUV = 1;
        m_AttribLocationVtxColor = 2;

        glGenBuffers(1, &m_VboHandle);
        glGenBuffers(1, &m_ElementsHandle);
        glGenVertexArrays(1, &m_VaoHandle);

        glBindVertexArray(m_VaoHandle);
        glBindBuffer(GL_ARRAY_BUFFER, m_VboHandle);
        glEnableVertexAttribArray(m_AttribLocationVtxPos);
        glEnableVertexAttribArray(m_AttribLocationVtxUV);
        glEnableVertexAttribArray(m_AttribLocationVtxColor);
        glVertexAttribPointer(m_AttribLocationVtxPos, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
            reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
        glVertexAttribPointer(m_AttribLocationVtxUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
            reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
        glVertexAttribPointer(m_AttribLocationVtxColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert),
            reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void ImGuiOpenGLRenderer::DestroyDeviceObjects()
    {
        if (m_VaoHandle != 0)
        {
            glDeleteVertexArrays(1, &m_VaoHandle);
            m_VaoHandle = 0;
        }

        if (m_VboHandle != 0)
        {
            glDeleteBuffers(1, &m_VboHandle);
            m_VboHandle = 0;
        }

        if (m_ElementsHandle != 0)
        {
            glDeleteBuffers(1, &m_ElementsHandle);
            m_ElementsHandle = 0;
        }

        if (m_ShaderHandle != 0)
        {
            if (m_VertHandle != 0)
            {
                glDetachShader(m_ShaderHandle, m_VertHandle);
            }
            if (m_FragHandle != 0)
            {
                glDetachShader(m_ShaderHandle, m_FragHandle);
            }
            glDeleteProgram(m_ShaderHandle);
            m_ShaderHandle = 0;
        }

        if (m_VertHandle != 0)
        {
            glDeleteShader(m_VertHandle);
            m_VertHandle = 0;
        }

        if (m_FragHandle != 0)
        {
            glDeleteShader(m_FragHandle);
            m_FragHandle = 0;
        }
    }

    void ImGuiOpenGLRenderer::CreateFontsTexture()
    {
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        glGenTextures(1, &m_FontTexture);
        glBindTexture(GL_TEXTURE_2D, m_FontTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        io.Fonts->SetTexID(static_cast<ImTextureID>(static_cast<intptr_t>(m_FontTexture)));
    }

    void ImGuiOpenGLRenderer::DestroyFontsTexture()
    {
        if (m_FontTexture != 0)
        {
            ImGui::GetIO().Fonts->SetTexID(0);
            glDeleteTextures(1, &m_FontTexture);
            m_FontTexture = 0;
        }
    }

    void ImGuiOpenGLRenderer::RenderDrawData()
    {
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData == nullptr || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
        {
            return;
        }

        const int framebufferWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
        const int framebufferHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
        if (framebufferWidth <= 0 || framebufferHeight <= 0)
        {
            return;
        }

        GLint lastProgram = 0;
        GLint lastTexture = 0;
        GLint lastArrayBuffer = 0;
        GLint lastElementArrayBuffer = 0;
        GLint lastVertexArray = 0;
        GLint lastBlendSrcRgb = 0;
        GLint lastBlendDstRgb = 0;
        GLint lastBlendSrcAlpha = 0;
        GLint lastBlendDstAlpha = 0;
        GLint lastBlendEquationRgb = 0;
        GLint lastBlendEquationAlpha = 0;
        GLint lastViewport[4] = {};
        GLint lastScissorBox[4] = {};
        GLboolean lastEnableBlend = glIsEnabled(GL_BLEND);
        GLboolean lastEnableCullFace = glIsEnabled(GL_CULL_FACE);
        GLboolean lastEnableDepthTest = glIsEnabled(GL_DEPTH_TEST);
        GLboolean lastEnableStencilTest = glIsEnabled(GL_STENCIL_TEST);
        GLboolean lastEnableScissorTest = glIsEnabled(GL_SCISSOR_TEST);

        glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &lastElementArrayBuffer);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVertexArray);
        glGetIntegerv(GL_BLEND_SRC_RGB, &lastBlendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &lastBlendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &lastBlendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &lastBlendDstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &lastBlendEquationRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &lastBlendEquationAlpha);
        glGetIntegerv(GL_VIEWPORT, lastViewport);
        glGetIntegerv(GL_SCISSOR_BOX, lastScissorBox);

        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_SCISSOR_TEST);
        glViewport(0, 0, framebufferWidth, framebufferHeight);

        const float left = drawData->DisplayPos.x;
        const float right = drawData->DisplayPos.x + drawData->DisplaySize.x;
        const float top = drawData->DisplayPos.y;
        const float bottom = drawData->DisplayPos.y + drawData->DisplaySize.y;
        const float orthoProjection[4][4] =
        {
            { 2.0f / (right - left), 0.0f, 0.0f, 0.0f },
            { 0.0f, 2.0f / (top - bottom), 0.0f, 0.0f },
            { 0.0f, 0.0f, -1.0f, 0.0f },
            { (right + left) / (left - right), (top + bottom) / (bottom - top), 0.0f, 1.0f },
        };

        glUseProgram(m_ShaderHandle);
        glUniform1i(m_AttribLocationTex, 0);
        glUniformMatrix4fv(m_AttribLocationProjMtx, 1, GL_FALSE, &orthoProjection[0][0]);
        glBindVertexArray(m_VaoHandle);

        for (int commandListIndex = 0; commandListIndex < drawData->CmdListsCount; commandListIndex++)
        {
            const ImDrawList* commandList = drawData->CmdLists[commandListIndex];
            glBindBuffer(GL_ARRAY_BUFFER, m_VboHandle);
            glBufferData(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(commandList->VtxBuffer.Size * sizeof(ImDrawVert)),
                commandList->VtxBuffer.Data,
                GL_STREAM_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ElementsHandle);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(commandList->IdxBuffer.Size * sizeof(ImDrawIdx)),
                commandList->IdxBuffer.Data,
                GL_STREAM_DRAW);

            int indexOffset = 0;
            for (const ImDrawCmd& command : commandList->CmdBuffer)
            {
                if (command.UserCallback != nullptr)
                {
                    command.UserCallback(commandList, &command);
                }
                else
                {
                    const ImVec2 clipMin(
                        (command.ClipRect.x - drawData->DisplayPos.x) * drawData->FramebufferScale.x,
                        (command.ClipRect.y - drawData->DisplayPos.y) * drawData->FramebufferScale.y);
                    const ImVec2 clipMax(
                        (command.ClipRect.z - drawData->DisplayPos.x) * drawData->FramebufferScale.x,
                        (command.ClipRect.w - drawData->DisplayPos.y) * drawData->FramebufferScale.y);

                    if (clipMax.x > clipMin.x && clipMax.y > clipMin.y)
                    {
                        glScissor(
                            static_cast<int>(clipMin.x),
                            static_cast<int>(framebufferHeight - clipMax.y),
                            static_cast<int>(clipMax.x - clipMin.x),
                            static_cast<int>(clipMax.y - clipMin.y));

                        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(command.GetTexID()));
                        glDrawElementsBaseVertex(
                            GL_TRIANGLES,
                            static_cast<GLsizei>(command.ElemCount),
                            sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                            reinterpret_cast<void*>(static_cast<intptr_t>(indexOffset * static_cast<int>(sizeof(ImDrawIdx)))),
                            static_cast<GLint>(command.VtxOffset));
                    }
                }

                indexOffset += static_cast<int>(command.ElemCount);
            }
        }

        glUseProgram(lastProgram);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(lastTexture));
        glBindVertexArray(static_cast<GLuint>(lastVertexArray));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(lastArrayBuffer));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(lastElementArrayBuffer));
        glBlendEquationSeparate(lastBlendEquationRgb, lastBlendEquationAlpha);
        glBlendFuncSeparate(lastBlendSrcRgb, lastBlendDstRgb, lastBlendSrcAlpha, lastBlendDstAlpha);

        lastEnableBlend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        lastEnableCullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
        lastEnableDepthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        lastEnableStencilTest ? glEnable(GL_STENCIL_TEST) : glDisable(GL_STENCIL_TEST);
        lastEnableScissorTest ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);

        glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
        glScissor(lastScissorBox[0], lastScissorBox[1], lastScissorBox[2], lastScissorBox[3]);
    }
}
