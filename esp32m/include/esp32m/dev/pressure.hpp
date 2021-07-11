#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <memory>

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace dev {

    class PressureSensor : public Device {
     public:
      PressureSensor(const char *name, io::IPin *pin);
      PressureSensor(const PressureSensor &) = delete;
      const char *name() const override {
        return _name;
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      virtual float compute(uint32_t mv, float r);

     private:
      const char *_name;
      io::pin::IADC *_adc;
      float _value = NAN;
      int _divisor = 0;
      unsigned long _stamp = 0;
    };

    PressureSensor *usePressureSensor(const char *name, io::IPin *pin);
  }  // namespace dev
}  // namespace esp32m