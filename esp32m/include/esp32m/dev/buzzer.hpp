#pragma once

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"
#include "esp32m/events/request.hpp"

#include <hal/gpio_types.h>

namespace esp32m {
  namespace dev {

    class Buzzer : public Device {
     public:
      Buzzer(io::IPin *pin);
      Buzzer(const Buzzer &) = delete;
      const char *name() const override {
        return "buzzer";
      }
      esp_err_t play(uint32_t freq, uint32_t duration);

     protected:
      bool handleRequest(Request &req) override;

     private:
      io::pin::ILEDC *_ledc;
    };

    Buzzer* useBuzzer(gpio_num_t pin);
    Buzzer* useBuzzer(io::IPin *pin);

  }  // namespace dev
}  // namespace esp32m