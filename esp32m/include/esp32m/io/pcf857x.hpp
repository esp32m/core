#pragma once

#include "esp32m/bus/i2c/master.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {
    class Pcf857x : public pin::ITxFinalizer, public IPins {
     public:
      enum Flavor {
        PCF8574,
        PCF8575,
      };
      Pcf857x(Flavor f, i2c::MasterDev *i2c);
      Pcf857x(const Pcf857x &) = delete;
      const char *name() const override {
        return "PCF857x";
      }
      Flavor flavor() const {
        return _flavor;
      }
      esp_err_t readPin(int pin, bool &value);
      esp_err_t writePin(int pin, bool value);
      esp_err_t setPinMode(int pin, bool input);

      esp_err_t read(uint16_t &port);
      esp_err_t write(uint16_t port);
      esp_err_t commit() override;

     protected:
      IPin *newPin(int id) override;

     private:
      Flavor _flavor;
      std::unique_ptr<i2c::MasterDev> _i2c;
      uint16_t _port = 0xFFFF, _inputMap = 0xFFFF;
      esp_err_t init();
    };

    Pcf857x *usePcf8574(uint8_t addr = 0x21);
    Pcf857x *usePcf8575(uint8_t addr = 0x20);

  }  // namespace io
}  // namespace esp32m