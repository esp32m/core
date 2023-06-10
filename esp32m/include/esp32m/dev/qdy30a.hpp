#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    class Qdy30a : public Device {
     public:
      enum Units {
        Unknown,
        Cm,
        Mm,
        MPa,
        Pa,
        KPa,
        Ma,
      };
      Qdy30a(uint8_t addr = 1);
      Qdy30a(const Qdy30a &) = delete;
      const char *name() const override {
        return "QDY30A";
      }
      float min() const {
        return _min;
      }
      float max() const {
        return _max;
      }
      float value() const {
        return _value;
      }
      Units units() const {
        return (Units)_units;
      }
      uint8_t decimals() const {
        return _decimals;
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;

     private:
      uint8_t _addr;
      uint16_t _regs[14] = {};
      uint8_t _units = 0, _decimals = 0;
      float _min = 0, _max = 0, _value = 0;
      unsigned long _stamp = 0;
      Sensor _sensor;
    };

    Qdy30a *useQdy30a(uint8_t addr = 1);
  }  // namespace dev
}  // namespace esp32m