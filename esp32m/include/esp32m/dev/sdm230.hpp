#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    class Sdm230 : public Device {
     public:
      Sdm230(uint8_t addr = 1);
      Sdm230(const Sdm230 &) = delete;
      const char *name() const override {
        return "SDM230";
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;

     private:
      uint8_t _addr;
      float _te = 0, _ee = 0, _ie = 0, _v = 0, _i = 0, _ap = 0, _rap = 0,
            _pf = 0, _f = 0;
      unsigned long _stamp;
    };
  }  // namespace dev
}  // namespace esp32m