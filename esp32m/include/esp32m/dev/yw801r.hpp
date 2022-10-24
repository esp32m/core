#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    class Yw801r : public Device {
     public:
      Yw801r(uint8_t addr = 192);
      Yw801r(const Yw801r &) = delete;
      const char *name() const override {
        return "YW801R";
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;

     private:
      uint8_t _addr;
      int16_t _pv = 0, _ad = 0;
      unsigned long _stamp = 0;
    };

    Yw801r *useYw801r(uint8_t addr = 192);
  }  // namespace dev
}  // namespace esp32m