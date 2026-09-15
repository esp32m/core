#pragma once

#include "esp32m/bus/i2c/master.hpp"
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

      // Register-compatible models with different electrical I/O capabilities.
      enum class Model { Pca9555, Pca9535, Pca9535C };

    }  // namespace pca95x5

    class Pca95x5 : public pin::ITxFinalizer, public IPins {
     public:
      Pca95x5(i2c::MasterDev *i2c,
              pca95x5::Bits bits = pca95x5::Bits::Sixteen,
              pca95x5::Model model = pca95x5::Model::Pca9555);
      Pca95x5(const Pca95x5 &) = delete;
      const char *name() const override {
        return "PCA95x5";
      }
      esp_err_t readPin(int pin, bool &value);
      esp_err_t writePin(int pin, bool value);
      esp_err_t setPinMode(int pin, bool input);

      pin::Flags pinFlags() const;
      // Explicit application-owned safe latch levels for reset/error recovery.
      // Automatic recovery requires coverage of every configured output pin.
      // A mismatch without this policy remains an error; unchanged readback
      // after a failed write permits a fresh operation without replaying it.
      // This only sets policy; it does not write the device.
      void setRecoveryOutput(uint16_t mask, uint16_t value);

      esp_err_t commit() override;
      void abort() override;

      esp_err_t read(pca95x5::Register reg, uint16_t &value);
      esp_err_t write(pca95x5::Register reg, uint16_t value);

     protected:
      IPin *newPin(int id) override;

     private:
      std::unique_ptr<i2c::MasterDev> _i2c;
      pca95x5::Bits _bits;
      pca95x5::Model _model;
      uint16_t _input = 0xFFFF, _output = 0xFFFF, _config = 0xFFFF;
      uint16_t _targetOutput = 0xFFFF, _inversion = 0;
      uint16_t _recoveryMask = 0, _recoveryOutput = 0;
      bool _initialized = false, _recoveryPending = false;
      char _verificationKey = 0;
      esp_err_t init();
      esp_err_t readRegister(pca95x5::Register reg, uint16_t &value);
      esp_err_t writeRegister(pca95x5::Register reg, uint16_t value);
      esp_err_t checkState(bool force = false);
      esp_err_t recover(uint16_t actualOutput);
      bool validPin(int pin) const;
      uint16_t mask() const;
    };

    Pca95x5 *usePca95x5(
        uint8_t addr = 0x20,
        pca95x5::Model model = pca95x5::Model::Pca9555);

  }  // namespace io
}  // namespace esp32m
