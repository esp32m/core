#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <memory>

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace dev {

    /**
     * This for 5V pressure sensor like this one:
     * https://aliexpress.com/item/1669537885.html The default settings are
     * for 1.2MPa with 10KOhm/10KOhm voltage divider Slope setting: 0.5MPa
     * - 1.6, 1.2MPa - 2/3, 5MPa - 0.16 Vdiv setting should match voltage drop
     * on voltage divider
     */
    class PressureSensor : public Device {
     public:
      PressureSensor(const char *name, io::IPin *pin);
      PressureSensor(const PressureSensor &) = delete;
      const char *name() const override {
        return _name;
      }
      float getSlope() const {
        return _slope;
      }
      void setSlope(float value) {
        _slope = value;
      }
      float getVdiv() const {
        return _vdiv;
      }
      void setVdiv(float value) {
        _vdiv = value;
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
      // voltage divider coeff
      float _vdiv = 2;  // voltage divider 2x
      float _slope = 2.0 / 3.0;
      int _divisor = 0;
      int _raw = 0;
      uint32_t _mv = 0;
      unsigned long _stamp = 0;
    };

    PressureSensor *usePressureSensor(const char *name, io::IPin *pin);
  }  // namespace dev
}  // namespace esp32m