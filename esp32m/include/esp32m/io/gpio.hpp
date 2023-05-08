#pragma once

#include <esp_adc/adc_oneshot.h>
#include <esp32m/io/pins.hpp>
#include <mutex>

namespace esp32m {
  namespace io {
    class Gpio : public IPins {
     public:
      const char *name() const override {
        return "GPIO";
      }
      static Gpio &instance();

     protected:
      io::IPin *newPin(int id) override;

     private:
      Gpio();
    };
  }  // namespace io

  namespace gpio {
    io::IPin *pin(gpio_num_t n);

  }  // namespace gpio
}  // namespace esp32m