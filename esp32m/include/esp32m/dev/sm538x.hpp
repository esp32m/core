#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    class Sm538x : public Device {
     public:
      Sm538x(uint8_t addr = 1);
      Sm538x(const Sm538x &) = delete;
      const char *name() const override {
        return _name;
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      const JsonObjectConst props() const override {
        return _props.as<JsonObjectConst>();
      }

     private:
      char _name[8] = "SM538x";
      StaticJsonDocument<JSON_OBJECT_SIZE(1)> _props;
      uint8_t _addr;
      uint16_t _model = 0;
      float _value = NAN;
      unsigned long _stamp = 0;
    };

    void useSm538x(uint8_t addr = 1);
  }  // namespace dev
}  // namespace esp32m