#pragma once

#include "esp32m/io/pins.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32m/defs.hpp"

#include "led_strip.h"

namespace esp32m {
  namespace dev {
    class Rmtled {
     public:
      Rmtled(gpio_num_t pin);
      Rmtled(const Rmtled &) = delete;

     private:
      gpio_num_t _pin;
      TaskHandle_t _task = nullptr;
      led_strip_handle_t led_strip;
      void configure_led();
      void init();
      void run();
      void toggle_led();
      void blink(int count);
    };
    Rmtled *useRmtled(gpio_num_t pin);
  }  // namespace dev
}  // namespace esp32m