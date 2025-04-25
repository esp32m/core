#pragma once

#include <ArduinoJson.h>
#include <hal/gpio_types.h>
#include <math.h>
#include <memory>

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace dev {
    class FlowSensor : public Device {
     public:
      FlowSensor(const char *name, io::IPin *pin);
      FlowSensor(const FlowSensor &) = delete;
      const char *name() const override {
        return _name;
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      const JsonVariantConst descriptor() const override {
        static JsonDocument/*<JSON_ARRAY_SIZE(1)>*/ doc;
        if (doc.isNull()) {
          auto root = doc.to<JsonArray>();
          root.add("flow");
        }
        return doc.as<JsonArrayConst>();
      };
      bool setConfig(RequestContext &ctx) override;
      JsonDocument *getConfig(RequestContext &ctx) override;
      JsonDocument *getState(RequestContext &ctx) override;
      virtual float compute(int pcnt, int ms);

     private:
      const char *_name;
      io::IPin *_pin;
      float _value = NAN;
      float _consumption = 0, _dumpedConsumption = 0;
      unsigned long _stamp = 0, _lastDump = 0;
      Sensor _sensorFlow, _sensorConsumption;
    };

    FlowSensor *useFlowSensor(const char *name, io::IPin *pin);
  }  // namespace dev
}  // namespace esp32m