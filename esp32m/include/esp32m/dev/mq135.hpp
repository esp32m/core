#pragma once

#include <ArduinoJson.h>
#include <hal/gpio_types.h>
#include <math.h>
#include <memory>

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace dev {
    class Mq135 : public Device {
     public:
      Mq135(io::IPin *pin, const char *name);
      Mq135(const Mq135 &) = delete;
      const char *name() const override {
        return _name ? _name : "MQ-135";
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      JsonDocument *getState(RequestContext &ctx) override;

     private:
      const char *_name;
      io::pin::IADC *_adc;
      int _divisor = 0;
      float _value = NAN;
      unsigned long _stamp = 0;
      Sensor _sensor;
    };

    Mq135 *useMq135(io::IPin *pin, const char *name = nullptr);
  }  // namespace dev
}  // namespace esp32m