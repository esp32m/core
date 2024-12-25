#pragma once

#include "esp32m/bus/i2c.hpp"
#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {

  namespace aht2x {

    const uint8_t DefaultAddress = 0x38;

    enum class Command {
      Status = 0x71,
    };

    class Core : public virtual log::Loggable {
     public:
      Core(const char *name) {
        _name = name;
      }
      Core(const Core &) = delete;
      const char *name() const override {
        return _name;
      }
      esp_err_t reset() {}

     protected:
      Core() {}
      std::unique_ptr<I2C> _i2c;
      const char *_name;

      esp_err_t init(I2C *i2c, const char *name = nullptr) {
        _i2c.reset(i2c);
        _name = name ? name : "AHT2X";
        delay(100);
        uint8_t status;
        ESP_CHECK_RETURN(getStatus(status));
      }

      esp_err_t cmd(Command cmd) {
        uint8_t c = (uint8_t)cmd;
        return _i2c->write(nullptr, 0, &c, sizeof(c));
      }
      esp_err_t getStatus(uint8_t &status) {
        uint8_t cmd = 0x71;
        return _i2c->read(&cmd, 1, &status, sizeof(status));
      }

     private:
    }

  }  // namespace aht2x
}  // namespace esp32m