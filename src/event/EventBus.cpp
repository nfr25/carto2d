#include "event/EventBus.h"

namespace e2d {

void EventBus::subscribe(std::string name, Handler handler)
{
    m_subscribers.push_back({std::move(name), std::move(handler)});
}

void EventBus::publish(const Event& event)
{
    const std::string name = event.value("event", std::string());
    for (const Subscriber& s : m_subscribers)
        if (s.name == "*" || s.name == name)
            s.handler(event);
}

} // namespace e2d
