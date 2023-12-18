#include "esp32m/sleep.hpp"
#include "esp32m/app.hpp"
#include "esp32m/base.hpp"

#include <esp_sleep.h>

namespace esp32m {
  namespace sleep {

    Mode _mode = Mode::Delay;
    void setMode(Mode mode) {
      _mode = mode;
    }
    bool Event::is(esp32m::Event &ev, Event **r) {
      if (!ev.is(Type))
        return false;
      if (r)
        *r = (Event *)&ev;
      return true;
    }

    Mode mode() {
      return _mode;
    }
    Mode effectiveMode() {
      Mode mode = _mode;
      if (mode != Mode::Delay) {
        Event ev(mode);
        ev.publish();
        if (ev.isBlocked())
          mode = Mode::Delay;
      }
      return mode;
    }

    void light(int us) {
      EventDone::publish(DoneReason::LightSleep);
      esp_sleep_enable_timer_wakeup(us);
      esp_light_sleep_start();
    }

    void deep(int us) {
      EventDone::publish(DoneReason::DeepSleep);
      esp_sleep_enable_timer_wakeup(us);
      esp_deep_sleep_start();
    }

    void ms(int delayMs, int lightMs, int deepMs) {
      switch (effectiveMode()) {
        case Mode::Delay:
          delay(delayMs);
          break;
        case Mode::Light:
          light(lightMs * 1000);
          break;
        case Mode::Deep:
          deep(deepMs * 1000);
          break;
      }
    }
    void ms(int ms) {
      switch (effectiveMode()) {
        case Mode::Delay:
          delay(ms);
          break;
        case Mode::Light:
          light(ms * 1000);
          break;
        case Mode::Deep:
          deep(ms * 1000);
          break;
      }
    }

    void us(int us) {
      switch (effectiveMode()) {
        case Mode::Delay:
          delayUs(us);
          break;
        case Mode::Light:
          light(us);
          break;
        case Mode::Deep:
          deep(us);
          break;
      }
    }
  }  // namespace sleep
}  // namespace esp32m