#pragma once

#include "Event/KeyCode.h"
#include "Event/MouseCode.h"
#include "Rice/Core.h"

#include <utility>

namespace Rice
{
    class RICE_API Input
    {
    private:
        static Input* s_Instance;
    protected:
        virtual bool IsKeyPressedImpl(KeyCode keyCode) = 0;

        virtual bool IsMouseButtonPressedImpl(MouseCode button) = 0;
        virtual std::pair<float, float> GetMousePositionImpl() = 0;
        virtual float GetMouseXImpl() = 0;
        virtual float GetMouseYImpl() = 0;
    public:
        inline static bool IsKeyPressed(KeyCode keyCode) { return s_Instance->IsKeyPressedImpl(keyCode); }
        
        inline static bool IsMouseButtonPressed(MouseCode button) { return s_Instance->IsMouseButtonPressedImpl(button); }
        inline static std::pair<float, float> GetMousePosition() { return s_Instance->GetMousePositionImpl(); }
        inline static float GetMouseX() { return s_Instance->GetMouseXImpl(); }
        inline static float GetMouseY() { return s_Instance->GetMouseYImpl(); }
    };
}
