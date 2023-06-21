#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    class Dds238 : public Device {
     public:
      Dds238(uint8_t addr = 1);
      Dds238(const Dds238 &) = delete;
      const char *name() const override {
        return "DDS238";
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;

     private:
      uint8_t _addr;
      float _te = 0, _ee = 0, _ie = 0, _v = 0, _i = 0, _ap = 0, _rap = 0,
            _pf = 0, _f = 0;
      unsigned long _stamp = 0;
      Sensor _energyImp, _energyExp, _voltage, _current, _powerApparent, _powerReactive, _powerFactor, _frequency;
    };

    Dds238* useDds238(uint8_t addr = 1);
  }  // namespace dev
}  // namespace esp32m