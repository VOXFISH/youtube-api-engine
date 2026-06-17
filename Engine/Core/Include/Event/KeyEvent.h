#pragma once

#include "Event.h"
#include "KeyCode.h"

namespace Rice {
    
    class KeyEvent : public Event
    {
    public:
        KeyCode GetKeyCode() const { return m_KeyCode; }
        EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)
        
    protected:
        KeyEvent(const KeyCode keycode) : m_KeyCode(keycode) {}

        KeyCode m_KeyCode;
    };

    class KeyPressedEvent : public KeyEvent
    {
    public:
        KeyPressedEvent(const KeyCode keycode, bool isRepeat = false)
            : KeyEvent(keycode), m_IsRepeat(isRepeat) {}
        
        bool IsRepeat() const { return m_IsRepeat; }

        std::string ToString() const {
            std::stringstream ss;
            ss << "KeyPressedEvent: " << KeyCodeToString(m_KeyCode) << " (repeat = " << m_IsRepeat << ")";
            return ss.str();
        }

        EVENT_CLASS_TYPE(KeyPressed)

    private:
        bool m_IsRepeat;
    };

    class KeyTypedEvent : public Event
    {
    public:
        explicit KeyTypedEvent(const std::uint32_t codepoint)
            : m_Codepoint(codepoint) {}

        std::uint32_t GetCodepoint() const { return m_Codepoint; }

        std::string ToString() const override
        {
            std::stringstream ss;
            ss << "KeyTypedEvent: U+" << std::hex << std::uppercase << m_Codepoint;
            return ss.str();
        }

        EVENT_CLASS_TYPE(KeyTyped)
        EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

    private:
        std::uint32_t m_Codepoint;
    };

    class KeyReleasedEvent : public KeyEvent
    {
    public:
        KeyReleasedEvent(const KeyCode keycode)
            : KeyEvent(keycode) {}

        std::string ToString() const override{
            std::stringstream ss;
            ss << "KeyReleasedEvent: " << KeyCodeToString(m_KeyCode);
            return ss.str();
        }

        EVENT_CLASS_TYPE(KeyReleased)
    };
}
