#pragma once

#include "esp32m/events.hpp"

#include <vector>

namespace esp32m {
  namespace sleep {

    enum class Mode { Delay, Light, Deep };

    class Event : public esp32m::Event {
     public:
      Event(Mode mode) : esp32m::Event(NAME), _mode(mode) {}
      Mode mode() const {
        return _mode;
      }
      static bool is(esp32m::Event &ev, Event **r);
      void block() {
        _blocked = true;
      };
      bool isBlocked() {
        return _blocked;
      }

     private:
      Mode _mode;
      bool _blocked = false;
      static const char *NAME;
    };

    Mode mode();
    Mode effectiveMode();
    void setMode(Mode mode);
    void ms(int ms);
    void us(int us);
    void ms(int delay, int light, int deep);
  }  // namespace sleep

  class Sleeper {
   public:
    Sleeper(int delay=1000, int light=30000, int deep=60000) {
      configure(delay, light, deep);
    }
    void configure(int delay, int light, int deep) {
      _delay = delay;
      _light = light;
      _deep = deep;
    }
    void sleep() {
      sleep::ms(_delay, _light, _deep);
    }

   private:
    int _delay, _light, _deep;
  };
}  // namespace esp32m