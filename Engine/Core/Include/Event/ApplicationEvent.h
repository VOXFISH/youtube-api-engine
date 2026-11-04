#pragma once

#include "Rice/Benchmark.h"
#include "Event.h"

#include <sstream>

namespace Rice
{
    class RICE_API WindowResizeEvent : public Event
    {
    private:
        unsigned int m_Width, m_Height;
    public:
        WindowResizeEvent(unsigned int width, unsigned int height)
        : m_Width(width), m_Height(height) { RICE_BENCHMARK_FUNCTION(); }

        inline unsigned int GetWidth() const { RICE_BENCHMARK_FUNCTION(); return m_Width;}
        inline unsigned int GetHeight() const { RICE_BENCHMARK_FUNCTION(); return m_Height;}

        std::string ToString() const override
        {
            RICE_BENCHMARK_FUNCTION();
            std::stringstream ss;
            ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;
            return ss.str();
        }

        EVENT_CLASS_TYPE(WindowResize)
        EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class RICE_API WindowCloseEvent : public Event
    {
    public:
        WindowCloseEvent(){ RICE_BENCHMARK_FUNCTION(); }

        EVENT_CLASS_TYPE(WindowClose)
        EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class RICE_API AppTickEvent : public Event
    {
    public:
        AppTickEvent(){ RICE_BENCHMARK_FUNCTION(); }

        EVENT_CLASS_TYPE(AppTick)
        EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class RICE_API AppUpdateEvent : public Event
    {
    public:
        AppUpdateEvent() { RICE_BENCHMARK_FUNCTION(); }
        
        EVENT_CLASS_TYPE(AppUpdate)
        EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class RICE_API AppRenderEvent : public Event{
    public:
        AppRenderEvent() { RICE_BENCHMARK_FUNCTION(); }
        
        EVENT_CLASS_TYPE(AppRender)
        EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };
}