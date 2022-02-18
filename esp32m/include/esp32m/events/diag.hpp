#pragma once

#include "esp32m/events.hpp"

namespace esp32m {
  namespace event {

    class Diag : public Event {
     public:
      Diag(const Diag &) = delete;
      uint8_t id() const {
        return _id;
      }
      uint8_t code() const {
        return _code;
      }

      static bool is(Event &ev, Diag **r);
      static void publish(uint8_t id, uint8_t code);

     private:
      uint8_t _id, _code;
      static const char *NAME;
      Diag(uint8_t id, uint8_t code) : Event(NAME), _id(id), _code(code){};
    };

  }  // namespace event
}  // namespace esp32m