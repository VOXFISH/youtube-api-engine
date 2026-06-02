#pragma once
#include "../Rice/PreCompiledHeaders.h"
#include "../Rice/Core.h"

/*
    외부 interrupt를 담당.
    현재는 interrupt 시 처리가 완료될 때까지 멈추지만
    유니티 시스템 등을 참고하여 입력을 버퍼할 예정.
*/
namespace Rice
{
    enum class EventType
    {
        None = 0,
        //창 변화
        WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,
        /*
        내부 렌더링 사이클
        AppTick : 주기 시작.
        FixedUpdate : 고정 시간마다 호출. 물리 엔진 구현 시 사용.
        Update : 매 프레임마다 호출.
        LateUpdate : 모든 update가 끝나고 나서 호출. 카메라 이동 계산 등.
        */
        AppTick, AppUpdate, AppFixedUpdate, AppLateUpdate, AppRender,
        //키보드 입력
        KeyPressed, KeyReleased,
        //마우스 입력
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
    };

    enum EventCategory
    {
        None = 0,
        EventCategoryApplication    = BIT(0),
        EventCategoryInput          = BIT(1),
        EventCategoryKeyboard       = BIT(2),
        EventCategoryMouse          = BIT(3),
        EventCategoryMouseButton    = BIT(4)
    };

    #define EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::type; }\
                                    virtual EventType GetEventType() const override { return GetStaticType(); }\
                                    virtual const char* Getname() const override { return #type; }

    #define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }


	class Event
	{
	public:
		virtual ~Event() = default;

		bool Handled = false;

		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;
		virtual std::string ToString() const { return GetName(); }

		bool IsInCategory(EventCategory category)
		{
			return GetCategoryFlags() & category;
		}
	};

	class EventDispatcher
	{
	public:
		EventDispatcher(Event& event)
			: m_Event(event)
		{
		}
		
		template<typename T, typename F>
		bool Dispatch(const F& func)
		{
			if (m_Event.GetEventType() == T::GetStaticType())
			{
				m_Event.Handled |= func(static_cast<T&>(m_Event));
				return true;
			}
			return false;
		}
	private:
		Event& m_Event;
	};

	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}
}