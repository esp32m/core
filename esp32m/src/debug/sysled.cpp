#include "esp32m/debug/sysled.hpp"
#include "esp32m/base.hpp"
#include "esp32m/debug/diag.hpp"
#include "esp32m/io/gpio.hpp"

#include <esp_task_wdt.h>

namespace esp32m {
  namespace debug {
    Sysled::Sysled() : _pin(gpio::pin(GPIO_NUM_2)) {
      init();
    }
    Sysled::Sysled(io::IPin *pin) : _pin(pin) {
      init();
    }

    void Sysled::init() {
      xTaskCreate([](void *self) { ((Sysled *)self)->run(); }, "m/sysled", 2048,
                  this, tskIDLE_PRIORITY + 1, &_task);
    }

    void Sysled::blink(int count) {
      auto digital = _pin->digital();
      if (digital) {
        digital->setDirection(GPIO_MODE_OUTPUT);
        for (int i = 0; i < count; i++) {
          digital->write(true);
          delay(200);
          digital->write(false);
          delay(200);
        }
      }
    }

    void Sysled::run() {
      uint8_t codes[10];
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        auto c = Diag::instance().toArray(codes, sizeof(codes));
        for (int i = 0; i < c; i++) {
          blink(codes[i]);
          delay(1000);
        }
        delay(2000);
      }
    }

    Sysled *useSysled() {
      static Sysled i;
      return &i;
    }

  }  // namespace debug
}  // namespace esp32m
