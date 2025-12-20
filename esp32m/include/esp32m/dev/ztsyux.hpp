#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    class Ztsyux : public Device {
     public:
      Ztsyux(uint8_t addr, const char *name);
      Ztsyux(const Ztsyux &) = delete;
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
      bool _value = false;
      unsigned long _stamp = 0;
      BinarySensor _sensor;
    };

    Ztsyux *useZtsyux(uint8_t addr = 1, const char *name = nullptr);
  }  // namespace dev
}  // namespace esp32m