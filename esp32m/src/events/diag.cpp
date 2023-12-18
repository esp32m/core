#include "esp32m/events/diag.hpp"

namespace esp32m {
  namespace event {

    bool Diag::is(Event &ev, Diag **r) {
      if (!ev.is(Type))
        return false;
      if (r)
        *r = (Diag *)&ev;
      return true;
    }

    void Diag::publish(uint8_t id, uint8_t code) {
      Diag ev(id, code);
      ev.Event::publish();
    }

  }  // namespace event
}  // namespace esp32m
