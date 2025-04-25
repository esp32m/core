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

    class Ntc : public Device {
     public:
      Ntc(const char *name, io::IPin *pin);
      Ntc(const Ntc &) = delete;
      const char *name() const override {
        return _name;
      }
      void configure(int rnom, int tnom, int beta, int rref, int samples,
                     float adj = 0) {
        _rnom = rnom;
        _tnom = tnom;
        _beta = beta;
        _rref = rref;
        _samples = samples;
        _adj = adj;
      }

     protected:
      bool pollSensors() override;
      JsonDocument *getState(RequestContext &ctx) override;

     private:
      const char *_name;
      io::pin::IADC *_adc;
      int _rnom = 10000, _tnom = 25, _beta = 3950, _rref = 10000, _samples = 5;
      float _adj = 0;
      Sensor _temperature;
      unsigned long _stamp = 0;
    };

    Ntc *useNtc(const char *name, io::IPin *pin);
  }  // namespace dev
}  // namespace esp32m