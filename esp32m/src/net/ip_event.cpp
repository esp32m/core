#include "esp32m/net/ip_event.hpp"

namespace esp32m {

  const char *IpEvent::NAME = "ip";

  bool IpEvent::is(ip_event_t event) const {
    return _event == event;
  }

  bool IpEvent::is(Event &ev, IpEvent **r) {
    if (!ev.is(NAME))
      return false;
    if (r)
      *r = (IpEvent *)&ev;
    return true;
  }

  bool IpEvent::is(Event &ev, ip_event_t event, IpEvent **r) {
    if (!ev.is(NAME))
      return false;
    ip_event_t t = ((IpEvent &)ev)._event;
    if (t != event)
      return false;
    if (r)
      *r = (IpEvent *)&ev;
    return true;
  }

  void IpEvent::publish(ip_event_t event, void *data) {
    IpEvent ev(event, data);
    EventManager::instance().publish(ev);
  }

}  // namespace esp32m