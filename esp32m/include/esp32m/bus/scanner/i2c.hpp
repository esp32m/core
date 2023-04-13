#pragma once

#include <hal/gpio_types.h>

#include "esp32m/app.hpp"

namespace esp32m {
  namespace bus {
    namespace scanner {

      class I2C : public AppObject {
       public:
        I2C(const I2C &) = delete;
        static I2C &instance();
        const char *name() const override {
          return "scanner-i2c";
        }
        const char *stateName() const override {
          return "i2c";
        }
        const char *interactiveName() const override {
          return "i2c";
        }

       protected:
        DynamicJsonDocument *getState(const JsonVariantConst args) override;
        bool handleRequest(Request &req) override;
        bool handleEvent(Event &ev) override;

       private:
        TaskHandle_t _task = nullptr;
        Response *_pendingResponse = nullptr;
        int _pinSDA = I2C_MASTER_SDA, _pinSCL = I2C_MASTER_SCL;
        int _startId = 3, _endId = 120;
        uint8_t _ids[16];
        uint32_t _freq = 100000;
        I2C();
        void run();
      };

      I2C *useI2C();
    }  // namespace scanner
  }    // namespace bus
}  // namespace esp32m
