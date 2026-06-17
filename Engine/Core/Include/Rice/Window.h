#pragma once

#include "PreCompiledHeaders.h"

#include "Core.h"
#include "Event/Event.h"

#include <functional>
#include <string>

namespace Rice
{
    struct WindowProps
    {
        std::string Title;
        unsigned int Width;
        unsigned int Height;

        WindowProps(const std::string& title = "Rice",
            unsigned int width = 1920,
            unsigned int height = 1080)
            : Title(title), Width(width), Height(height)
        {
        }
    };

    class RICE_API Window
    {
    public:
        using EventCallbackFn = std::function<void(Event&)>;

        virtual ~Window() = default;

        virtual void OnUpdate() = 0;
        virtual void SwapBuffers() = 0;

        virtual unsigned int GetWidth() const = 0;
        virtual unsigned int GetHeight() const = 0;

        virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
        virtual void SetVSync(bool enabled) = 0;
        virtual bool IsVSync() const = 0;
        virtual void* GetNativeWindow() const = 0;

        static Window* Create(const WindowProps& props = WindowProps());
    };
}
