#pragma once

#include "Rice/Core.h"
#include "Rice/Time.h"
#include "Event/Event.h"

namespace Rice
{
    class RICE_API Layer
    {
    public:
        Layer(const std::string& name = "Layer");
        virtual ~Layer();

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(Time) {}
        virtual void OnImGuiRender() {}
        virtual void OnEvent(Event&) {}

        inline const std::string& Getname() const {return m_DebugName;}

    protected:
        std::string m_DebugName;
    };
}
