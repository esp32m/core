#pragma once

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <memory>

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"
#include "esp32m/io/utils.hpp"

namespace esp32m {
  namespace dev {

    class MicrowaveMotionSensor : public Device {
     public:
      MicrowaveMotionSensor(const char *name, io::IPin *pin);
      MicrowaveMotionSensor(const MicrowaveMotionSensor &) = delete;
      const char *name() const override {
        return _name;
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;

     private:
      const char *_name;
      io::pin::IADC *_adc;
      TaskHandle_t _task = nullptr;
      io::Sampler *_sampler;
      int _divisor = 0;
      unsigned long _stamp = 0;
      void run();
    };

    MicrowaveMotionSensor *useMicrowaveMotionSensor(const char *name,
                                                    io::IPin *pin);
  }  // namespace dev
}  // namespace esp32m