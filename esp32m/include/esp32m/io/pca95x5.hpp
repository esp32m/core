#pragma once

#include "esp32m/bus/i2c.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {

    namespace pca95x5 {

      enum class Register {
        Input = 0,
        Output = 2,
        Inversion = 4,
        Config = 6,
      };

    }

    class Pca95x5 : public pin::ITxFinalizer, public IPins {
     public:
      Pca95x5(I2C *i2c);
      Pca95x5(const Pca95x5 &) = delete;
      const char *name() const override {
        return "PCA95x5";
      }
      esp_err_t readPin(int pin, bool &value);
      esp_err_t writePin(int pin, bool value);
      esp_err_t setPinMode(int pin, bool input);

      esp_err_t commit() override;

      esp_err_t read(pca95x5::Register reg, uint16_t &value);
      esp_err_t write(pca95x5::Register reg, uint16_t value);
     protected:
      IPin *newPin(int id) override;

     private:
      std::unique_ptr<I2C> _i2c;
      uint16_t _port = 0xFFFF, _inputMap = 0xFFFF;
      esp_err_t init();
    };

    Pca95x5 *usePca95x5(uint8_t addr = 0x20);

  }  // namespace io
}  // namespace esp32m