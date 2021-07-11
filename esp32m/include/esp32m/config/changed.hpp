#pragma once

#include "esp32m/config/configurable.hpp"
#include "esp32m/events.hpp"

namespace esp32m {

  class EventConfigChanged : public Event {
   public:
    Configurable *configurable() const {
      return _configurable;
    }
    bool saveNow() const {
      return _saveNow;
    }
    static void publish(Configurable *configurable, bool saveNow = false) {
      EventConfigChanged evt(configurable, saveNow);
      evt.Event::publish();
    }
    static bool is(Event &ev) {
      return ev.is(NAME);
    }

   private:
    EventConfigChanged(Configurable *configurable, bool saveNow)
        : Event(NAME), _configurable(configurable), _saveNow(saveNow) {}
    Configurable *_configurable;
    bool _saveNow;
    static const char *NAME;
  };
}  // namespace esp32m