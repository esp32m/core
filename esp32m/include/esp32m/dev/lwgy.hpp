#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    class Lwgy : public Device {
     public:
      Lwgy(uint8_t addr, const char *name);
      Lwgy(const Lwgy &) = delete;
      const char *name() const override {
        return _name;
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      bool handleRequest(Request &req) override;
      JsonDocument *getState(RequestContext &ctx) override;
      bool setConfig(RequestContext &ctx) override;
      JsonDocument *getConfig(RequestContext &ctx) override;

     private:
      const char *_name;
      uint8_t _addr;
      uint16_t _decimals = 0, _density = 0, _flowunit = 0, _id = 0;
      float _range = 0, _mc = 0;
      unsigned long _stamp = 0;
      Sensor _flow, _consumption;
    };

    Lwgy *useLwgy(uint8_t addr = 1, const char *name = nullptr);
  }  // namespace dev
}  // namespace esp32m