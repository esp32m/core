#pragma once

#include <esp_netif.h>

#include "esp32m/events.hpp"

namespace esp32m {
  class IpEvent : public Event {
   public:
    ip_event_t event() const {
      return _event;
    }
    void *data() const {
      return _data;
    }

    bool is(ip_event_t event) const;

    static bool is(Event &ev, IpEvent **r);
    static bool is(Event &ev, ip_event_t event, IpEvent **r);

    static void publish(ip_event_t event, void *data);

   protected:
    static const char *NAME;

   private:
    ip_event_t _event;
    void *_data;
    IpEvent(ip_event_t event, void *data)
        : Event(NAME), _event(event), _data(data) {}
    friend class Wifi;
  };

}  // namespace esp32m