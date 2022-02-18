#pragma once

#include "esp32m/io/pins.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esp32m {
  namespace debug {
    class Sysled {
     public:
      Sysled();
      Sysled(io::IPin *pin);
      Sysled(const Sysled &) = delete;

     private:
      io::IPin *_pin;
      TaskHandle_t _task = nullptr;
      void init();
      void run();
      void blink(int count);
    };
    Sysled* useSysled();
  }  // namespace debug
}  // namespace esp32m