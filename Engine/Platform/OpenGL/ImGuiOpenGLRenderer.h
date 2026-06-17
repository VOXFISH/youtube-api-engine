#pragma once

#include "Event/Event.h"

struct GLFWwindow;

namespace Rice
{
    class ImGuiOpenGLRenderer
    {
    public:
        ImGuiOpenGLRenderer() = default;
        ~ImGuiOpenGLRenderer() = default;

        void Init(GLFWwindow* window, const char* glslVersion = nullptr);
        void Shutdown();

        void BeginFrame();
        void EndFrame();
        void OnEvent(Event& event);

        bool WantsCaptureMouse() const;
        bool WantsCaptureKeyboard() const;

    private:
        void CreateDeviceObjects();
        void DestroyDeviceObjects();
        void CreateFontsTexture();
        void DestroyFontsTexture();
        void RenderDrawData();

        GLFWwindow* m_Window = nullptr;
        const char* m_GlslVersion = nullptr;
        double m_Time = 0.0;
        bool m_Initialized = false;

        unsigned int m_FontTexture = 0;
        unsigned int m_ShaderHandle = 0;
        unsigned int m_VertHandle = 0;
        unsigned int m_FragHandle = 0;
        int m_AttribLocationTex = 0;
        int m_AttribLocationProjMtx = 0;
        unsigned int m_AttribLocationVtxPos = 0;
        unsigned int m_AttribLocationVtxUV = 0;
        unsigned int m_AttribLocationVtxColor = 0;
        unsigned int m_VboHandle = 0;
        unsigned int m_ElementsHandle = 0;
        unsigned int m_VaoHandle = 0;
    };
}
