#pragma once

#include <hal/gpio_types.h>

#include "esp32m/device.hpp"
#include "esp32m/events/request.hpp"

namespace esp32m {
  namespace dev {

    class Buzzer : public Device {
     public:
      Buzzer(gpio_num_t pin);
      Buzzer(const Buzzer &) = delete;
      const char *name() const override {
        return "buzzer";
      }
      void play(uint32_t freq, uint32_t duration);

     protected:
      bool handleRequest(Request &req) override;

     private:
      gpio_num_t _pin;
    };

    void useBuzzer(gpio_num_t pin);

  }  // namespace dev
}  // namespace esp32m