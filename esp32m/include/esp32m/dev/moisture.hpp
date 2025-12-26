#pragma once

#include <ArduinoJson.h>
#include <hal/gpio_types.h>
#include <math.h>
#include <memory>

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace dev {
    class MoistureSensor : public Device {
     public:
      MoistureSensor(const char *name, io::IPin *pin);
      MoistureSensor(const MoistureSensor &) = delete;
      const char *name() const override {
        return _name;
      }

     protected:
      const JsonVariantConst descriptor() const override {
        static JsonDocument/*<JSON_ARRAY_SIZE(1)>*/ doc;
        if (doc.isNull()) {
          auto root = doc.to<JsonArray>();
          root.add("moisture");
        }
        return doc.as<JsonArrayConst>();
      };
      bool pollSensors() override;
      bool initSensors() override;
      JsonDocument *getState(RequestContext &ctx) override;

     private:
      const char *_name;
      io::pin::IADC *_adc;
      int _divisor = 0;
      uint32_t _mv = 0;
      float _value = NAN;
      unsigned long _stamp = 0;
      Sensor _moisture;
    };

    MoistureSensor *useMoistureSensor(const char *name, io::IPin *pin);
  }  // namespace dev
}  // namespace esp32m