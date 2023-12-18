#include "esp32m/debug/button.hpp"
#include "esp32m/base.hpp"
#include "esp32m/io/gpio.hpp"

#include <esp_task_wdt.h>

namespace esp32m {
  namespace debug {

    Button::Button(gpio_num_t pin) : _pin(gpio::pin(pin)) {
      xTaskCreate([](void *self) { ((Button *)self)->run(); }, "m/dbgbtn", 4096,
                  this, tskIDLE_PRIORITY + 1, &_task);
    }

    void Button::run() {
      _queue = xQueueCreate(16, sizeof(int32_t));
      _pin->digital()->attach(_queue);
      esp_task_wdt_add(NULL);
      int32_t value;
      for (;;) {
        esp_task_wdt_reset();
        if (xQueueReceive(_queue, &value, pdMS_TO_TICKS(1000))) {
          _stamp = millis();
          if (value < 0) {
            value = -value;
            value = value / 1000;
            if (value > 10 && value < 3000) {
              _cmd <<= 1;
              if (value > 700)
                _cmd |= 1;
            }
          }
        } else if (_cmd && (millis() - _stamp) > 3000) {
          logD("command %d issued", _cmd);
          button::Command ev(_cmd);
          _cmd = 0;
          ev.publish();
        }
      }
    }

    Button *useButton(gpio_num_t pin) {
      return new Button(pin);
    }

  }  // namespace debug
}  // namespace esp32m
