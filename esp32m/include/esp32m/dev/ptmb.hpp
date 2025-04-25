#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    class Ptmb: public Device {
     public:
      Ptmb(uint8_t addr, const char *name);
      Ptmb(const Ptmb&) = delete;
      const char *name() const override {
        return _name;
      }
      bool value() const {
        return _value;
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      JsonDocument *getState(RequestContext &ctx) override;
      bool setConfig(RequestContext &ctx) override;
      JsonDocument *getConfig(RequestContext &ctx) override;

     private:
      const char *_name;
      uint8_t _addr;
      float _value = 0;
      unsigned long _stamp = 0;
      Sensor _sensor;
    };

    Ptmb *usePtmb(uint8_t addr = 1, const char *name = nullptr);
  }  // namespace dev
}  // namespace esp32m