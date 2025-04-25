#pragma once

#include <ArduinoJson.h>
#include <hal/gpio_types.h>
#include <math.h>
#include <memory>

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace dev {
    class Fc37 : public Device {
     public:
      Fc37(io::IPin *pin);
      Fc37(const Fc37 &) = delete;
      const char *name() const override {
        return "FC-37";
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      JsonDocument *getState(RequestContext &ctx) override;

     private:
      io::pin::IADC *_adc;
      int _divisor = 0;
      float _value = NAN;
      unsigned long _stamp = 0;
      Sensor _sensor;
    };

    Fc37 *useFc37(io::IPin *pin);
  }  // namespace dev
}  // namespace esp32m