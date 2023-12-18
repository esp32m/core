#pragma once

#include "esp32m/device.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/io/pins.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32m/defs.hpp"

#include "led_strip.h"

namespace esp32m {
  namespace dev {
    class Rmtled : public Device {
     public:
      Rmtled(gpio_num_t pin);
      Rmtled(const Rmtled &) = delete;
      const char *name() const override {
        return "rmtled";
      }
     protected:
      bool handleRequest(Request &req) override; 

     private:
      gpio_num_t _pin;
      uint32_t _led_color[3] = { 0, 0, 16 };  // Red, Green, Blue  (0-255) 
      TaskHandle_t _task = nullptr;
      led_strip_handle_t led_strip;
      void configure_led();
      void init();
      void run();
      void blink(int count);
    };

    Rmtled *useRmtled(gpio_num_t pin);

  }  // namespace dev
}  // namespace esp32m