#pragma once

#include "esp32m/bus/i2c/master.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {
    namespace aw9523 {
      enum class PinMode { Input, Output, DAC };
    }
    class Aw9523 : public pin::ITxFinalizer, public IPins {
     public:
      Aw9523(i2c::MasterDev *i2c);
      Aw9523(const Aw9523 &) = delete;
      const char *name() const override {
        return "AW9523";
      }

      esp_err_t setRst(io::IPin *pin);

      esp_err_t reset(bool hard = false);

      esp_err_t readPin(int pin, bool &value);
      esp_err_t writePin(int pin, bool value);
      esp_err_t setPinMode(int pin, aw9523::PinMode mode);
      esp_err_t analogWrite(int pin, float value);
      esp_err_t commit() override;
      esp_err_t setP0PushPull(bool enable);

     protected:
      IPin *newPin(int id) override;

     private:
      std::unique_ptr<i2c::MasterDev> _i2c;
      io::pin::IDigital *_rst = nullptr;
      uint16_t _out, _cfg, _int, _mode, _input, _gcr;
      esp_err_t init();
      esp_err_t readInput();
      esp_err_t writeOutput();
      esp_err_t updateGcr(uint16_t gcr);
    };

    Aw9523 *useAw9523(uint8_t addr = 0x58);

  }  // namespace io
}  // namespace esp32m