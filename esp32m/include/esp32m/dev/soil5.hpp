#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    class Soil5 : public Device {
     public:
      Soil5(uint8_t addr, const char *name);
      Soil5(const Soil5 &) = delete;
      const char *name() const override {
        return _name;
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      JsonDocument *getState(RequestContext &ctx) override;

     private:
      const char *_name;
      uint8_t _addr;
      unsigned long _stamp = 0;
      Sensor _moisture, _temperature, _conductivity, _ph, _nitrogen, _phosphorus, _potassium, _salinity, _tds;
    };

    Soil5 *useSoil5(uint8_t addr = 1, const char *name = nullptr);
  }  // namespace dev
}  // namespace esp32m