#pragma once

#include "esp32m/device.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/io/pins.hpp"

#include <hal/gpio_types.h>

namespace esp32m {
  namespace dev {
    class Servo : public Device {
     public:
      Servo(const char *name, io::IPin *pin);
      Servo(const Servo &) = delete;
      const char *name() const override {
        return _name;
      }
      esp_err_t config(int pwMin = 500, int pwMax = 2500, int freq = 50,
                       ledc_timer_bit_t timerRes = LEDC_TIMER_15_BIT);
      esp_err_t setAngle(float angle);
      float getAngle();
      esp_err_t setPulseWidth(int us);
      int getPulseWidth();
      void setPersistent(bool p) {
        _persistent = p;
      }
      bool isPersistent() const {
        return _persistent;
      }

     protected:
      const JsonVariantConst descriptor() const override {
        static StaticJsonDocument<JSON_ARRAY_SIZE(1)> doc;
        if (doc.isNull()) {
          auto root = doc.to<JsonArray>();
          root.add("servo");
        }
        return doc.as<JsonArrayConst>();
      };
      void setState(const JsonVariantConst cfg,
                    DynamicJsonDocument **result) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;

     private:
      const char *_name;
      int _pwMin = 500, _pwMax = 2500, _freq = 50;
      ledc_timer_bit_t _timerRes = LEDC_TIMER_15_BIT;
      bool _configDirty;
      io::IPin *_pin;
      io::pin::ILEDC *_ledc;
      int _pulseWidth = 0;
      bool _persistent = true;
      int usToTicks(int usec);
      int ticksToUs(int ticks);
    };

    Servo *useServo(const char *name, io::IPin *pin);
  }  // namespace dev
}  // namespace esp32m