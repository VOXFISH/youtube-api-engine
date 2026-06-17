#pragma once
#include "Input/Input.h"

namespace Rice
{
    class WindowsInput : public Input
    {
    protected:
        bool IsKeyPressedImpl(KeyCode keyCode) override;
        bool IsMouseButtonPressedImpl(MouseCode button) override;
        std::pair<float, float> GetMousePositionImpl() override;
        float GetMouseXImpl() override;
        float GetMouseYImpl() override;
    };
}
