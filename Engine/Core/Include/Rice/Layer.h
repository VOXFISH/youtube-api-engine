#pragma once

#include "Rice/Core.h"
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
        virtual void OnUpdate() {}
        virtual void OnEvent(Event&) {}

        inline const std::string& Getname() const {return m_DebugName;}

    protected:
        std::string m_DebugName;
    };
}
