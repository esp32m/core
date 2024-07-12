#pragma once

#include "esp32m/io/pins.hpp"
#include "esp_err.h"

namespace esp32m {
  namespace io {

    class CurrentSensor {
     public:
      CurrentSensor(io::pin::IADC *adc, float shuntOhm)
          : _adc(adc), _shuntOhm(shuntOhm) {}
      CurrentSensor(const CurrentSensor &) = delete;
      void setShunt(float ohm) {
        _shuntOhm = ohm;
      }
      float getShunt() const {
        return _shuntOhm;
      }

      esp_err_t read(float &current) {
        int value;
        uint32_t mv;
        ESP_CHECK_RETURN(_adc->read(value, &mv));
        current = compute(mv / 1000.0f);
        return ESP_OK;
      }

     protected:
      io::pin::IADC *_adc;
      float _shuntOhm;
      virtual float compute(float volts) {
        if (_shuntOhm == 0)
          return 0;
        return volts / _shuntOhm;
      }
    };
  }  // namespace io
}  // namespace esp32m