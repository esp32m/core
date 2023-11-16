#include "esp32m/dev/ntc.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

#include <esp_task_wdt.h>
#include "math.h"

namespace esp32m {
  namespace dev {

    Ntc::Ntc(const char *name, io::IPin *pin)
        : _name(name), _temperature(this, "temperature") {
      _adc = pin ? pin->adc() : nullptr;
      _temperature.precision = 2;
      Device::init(Flags::HasSensors);
    }

    DynamicJsonDocument *Ntc::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(2));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_temperature.get());
      return doc;
    }

    bool Ntc::pollSensors() {
      if (!_adc)
        return false;
      int samples = 0;
      float average = 0;
      for (int i = 0; i < _samples; i++) {
        float value;
        uint32_t mv;
        if (_adc->read(value, &mv) == ESP_OK) {
          average += value;
          samples++;
          delay(1);
        }
      }
      if (samples) {
        average /= samples;
        average = _rref / average;

        float t = average / _rnom;    // (R/Ro)
        t = ::log(t);                 // ln(R/Ro)
        t /= _beta;                   // 1/B * ln(R/Ro)
        t += 1.0 / (_tnom + 273.15);  // + (1/To)
        t = 1.0 / t;                  // Invert
        t -= 273.15;                  // convert absolute temp to C
        _temperature.set(t);
        _stamp = millis();
      }
      return true;
    }

    Ntc *useNtc(const char *name, io::IPin *pin) {
      return new Ntc(name, pin);
    }
  }  // namespace dev
}  // namespace esp32m