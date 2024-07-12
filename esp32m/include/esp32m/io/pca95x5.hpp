#pragma once

#include "esp32m/bus/i2c.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {

    namespace pca95x5 {

      enum class Register {
        Input,
        Output,
        Inversion,
        Config,
      };

      enum class Bits {
        Sixteen,
        Eight,
      };

    }  // namespace pca95x5

    class Pca95x5 : public pin::ITxFinalizer, public IPins {
     public:
      Pca95x5(I2C *i2c, pca95x5::Bits bits = pca95x5::Bits::Sixteen);
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
      pca95x5::Bits _bits;
      uint16_t _input = 0xFFFF, _output = 0x0, _config = 0xFFFF, _targetOutput=0;
      esp_err_t init();
    };

    Pca95x5 *usePca95x5(uint8_t addr = 0x20);

  }  // namespace io
}  // namespace esp32m