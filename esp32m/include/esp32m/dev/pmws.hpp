#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    /**
     * Driver for VemSee SN-*-PM/PMWS-N01 air quality transmitter.
     * Measures PM2.5, PM10, PM1.0 via laser scattering.
     * PMWS variant additionally measures temperature and humidity.
     * Communication: RS-485, Modbus-RTU, default 4800 baud, address 0x01.
     */
    class Pmws : public Device {
     public:
      enum class Mode {
        Pmws,  // PM + temperature + humidity (registers 0x0000..0x0004)
        Pm,    // PM only (registers 0x0000..0x0002)
      };

      Pmws(uint8_t addr, const char *name = nullptr, Mode mode = Mode::Pmws);
      Pmws(const Pmws &) = delete;
      const char *name() const override {
        return _name;
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
      Mode _mode;
      unsigned long _stamp = 0;
      Sensor _pm25, _pm10, _pm1;
      Sensor *_temperature = nullptr;
      Sensor *_humidity = nullptr;
    };

    Pmws *usePmws(uint8_t addr = 1, const char *name = nullptr);
    Pmws *usePm(uint8_t addr = 1, const char *name = nullptr);
  }  // namespace dev
}  // namespace esp32m
