#pragma once

#include <ArduinoJson.h>

#include "esp32m/events.hpp"

namespace esp32m {
  class Broadcast : public Event {
   public:
    const char *source() const {
      return _source;
    }
    const char *name() const {
      return _name;
    }
    const JsonVariantConst data() const {
      return _data;
    }

    static bool is(Event &ev, Broadcast **r);
    static void publish(const char *source, const char *name);
    static void publish(const char *source, const char *name,
                        const JsonVariantConst data);

   protected:
    Broadcast(const char *source, const char *name, const JsonVariantConst data)
        : Event(Type), _source(source), _name(name), _data(data) {}
    constexpr static const char *Type = "broadcast";

   private:
    const char *_source;
    const char *_name;
    const JsonVariantConst _data;
  };

}  // namespace esp32m