#include "esp32m/events/broadcast.hpp"
#include "esp32m/json.hpp"

namespace esp32m {

  const char *Broadcast::NAME = "broadcast";

  bool Broadcast::is(Event &ev, Broadcast **r) {
    if (!ev.is(NAME))
      return false;
    if (r)
      *r = (Broadcast *)&ev;
    return true;
  }

  void Broadcast::publish(const char *source, const char *name,
                          const JsonVariantConst data) {
    Broadcast ev(source, name, data);
    ev.Event::publish();
  }

  void Broadcast::publish(const char *source, const char *name) {
    Broadcast ev(source, name, json::null<JsonVariantConst>());
    ev.Event::publish();
  }
}  // namespace esp32m
