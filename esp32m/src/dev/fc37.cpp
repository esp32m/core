#include "esp32m/dev/fc37.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {

    Fc37::Fc37(io::IPin *pin) : _sensor(this, "rain") {
      Device::init(Flags::HasSensors);
      _sensor.unit = "%";
      _sensor.precision = 1;
      _adc = pin ? pin->adc() : nullptr;
    }

    bool Fc37::initSensors() {
      if (!_adc)
        return false;
      int min, max;
      ESP_CHECK_RETURN_BOOL(_adc->range(min, max));
      _divisor = max - min;
      return _divisor != 0;
    }

    JsonDocument *Fc37::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(2) */
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_value);
      return doc;
    }

    bool Fc37::pollSensors() {
      if (!_adc)
        return false;
      int raw;
      auto err = ESP_ERROR_CHECK_WITHOUT_ABORT(_adc->read(raw));
      if (err == ESP_OK) {
        _value = (float)raw / _divisor;
        _stamp = millis();
        sensor("value", _value);
      }
      return true;
    }

    Fc37 *useFc37(io::IPin *pin) {
      return new Fc37(pin);
    }
  }  // namespace dev
}  // namespace esp32m