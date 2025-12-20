#include "esp32m/dev/ntc.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

#include <esp_task_wdt.h>
#include "math.h"

namespace esp32m {
  namespace dev {

    Ntc::Ntc(const char* name, io::IPin* pin)
        : _name(name), _temperature(this, "temperature") {
      _adc = pin ? pin->adc() : nullptr;
      _temperature.precision = 1;
      Device::init(Flags::HasSensors);
    }

    JsonDocument* Ntc::getState(RequestContext& ctx) {
      JsonDocument* doc = new JsonDocument(); /* JSON_ARRAY_SIZE(2) */
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_temperature.get());

      float value;
      uint32_t mv;
      if (_adc->read(value, &mv) == ESP_OK) {
        arr.add(value);
        arr.add(mv);
      }
      return doc;
    }

    bool Ntc::pollSensors() {
      if (!_adc)
        return false;
      int samples = 0;
      float average = 0;
      float averageMv = 0;
      for (int i = 0; i < _samples; i++) {
        float value;
        uint32_t mv;
        if (_adc->read(value, &mv) == ESP_OK) {
          average += value;
          averageMv += mv;
          samples++;
          delay(1);
        }
      }
      if (samples) {
        float t;
        if (1) {
          averageMv /= samples;
          float rntc = ((float)_rref * (averageMv / (3300.0f - averageMv)));
          float Tk = 1.0f / ((1.0f / (_tnom + 273.15f)) +
                             (1.0f / _beta) * ::log(rntc / _rnom));
          t = Tk - 273.15f;  // convert absolute temp to C
        } else {
          average /= samples;
          average = _rref / (1.0 / average - 1.0);

          t = average / _rnom;          // (R/Ro)
          t = ::log(t);                 // ln(R/Ro)
          t /= _beta;                   // 1/B * ln(R/Ro)
          t += 1.0 / (_tnom + 273.15);  // + (1/To)
          t = 1.0 / t;                  // Invert
          t -= 273.15;                  // convert absolute temp to C
        }
        _temperature.set(t + _adj);
        _stamp = millis();
      }
      return true;
    }

    Ntc* useNtc(const char* name, io::IPin* pin) {
      return new Ntc(name, pin);
    }
  }  // namespace dev
}  // namespace esp32m