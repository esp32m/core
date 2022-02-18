#include "esp32m/dev/mq135.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {

    Mq135::Mq135(io::IPin *pin, const char *name)
        : Device(Flags::HasSensors), _name(name) {
      _adc = pin ? pin->adc() : nullptr;
    }

    bool Mq135::initSensors() {
      if (!_adc)
        return false;
      int min, max;
      ESP_CHECK_RETURN_BOOL(_adc->range(min, max));
      _divisor = max - min;
      return _divisor != 0;
    }

    DynamicJsonDocument *Mq135::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(2));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_value);
      return doc;
    }

    bool Mq135::pollSensors() {
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

    Mq135 *useMq135(io::IPin *pin, const char *name) {
      return new Mq135(pin, name);
    }
  }  // namespace dev
}  // namespace esp32m