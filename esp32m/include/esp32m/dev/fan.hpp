#pragma once

#include <ArduinoJson.h>
#include <hal/gpio_types.h>
#include <math.h>
#include <memory>

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace dev {

    class Fan : public Device {
     public:
      Fan(const char *name, io::IPin *power, io::IPin *tach = nullptr);
      Fan(const Fan &) = delete;
      virtual ~Fan();
      const char *name() const override {
        return _name;
      }

      esp_err_t configurePwm(io::IPin *pwm, uint32_t freq);
      esp_err_t setSpeed(float speed);
      void setPulsesPerRevolution(int value) {
        _ppr = value < 1 ? 1 : value;
      }
      int getPulsesPerRevolution() const {
        return _ppr;
      }
      esp_err_t turn(bool on);

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      const JsonVariantConst descriptor() const override {
        static JsonDocument/*<JSON_ARRAY_SIZE(1)>*/ doc;
        if (doc.isNull()) {
          auto root = doc.to<JsonArray>();
          root.add("fan");
        }
        return doc.as<JsonArrayConst>();
      };
      bool setConfig(RequestContext &ctx) override;
      JsonDocument *getConfig(RequestContext &ctx) override;
      JsonDocument *getState(RequestContext &ctx) override;
      void setState(RequestContext &ctx) override;

     private:
      const char *_name;
      io::IPin *_power, *_tach, *_pwm = nullptr;
      uint32_t _freq = 0, _ppr = 1;
      float _rpm = NAN, _speed = 0;
      bool _on = false;
      unsigned long _stamp = 0;
      Sensor *_sensorRpm = nullptr;
      esp_err_t update();
      esp_err_t applyConfig();
    };

    Fan *useFan(const char *name, io::IPin *power, io::IPin *tach = nullptr);
  }  // namespace dev
}  // namespace esp32m