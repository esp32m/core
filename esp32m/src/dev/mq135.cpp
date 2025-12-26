#include "esp32m/dev/mq135.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {

    Mq135::Mq135(io::IPin *pin, const char *name)
        : _name(name), _sensor(this, "volatile_organic_compounds_parts", "voc") {
      Device::init(Flags::HasSensors);
      _adc = pin ? pin->adc() : nullptr;
      _sensor.unit = "ppm";
      _sensor.precision = 2;
    }

    bool Mq135::initSensors() {
      if (!_adc)
        return false;
      int min, max;
      ESP_CHECK_RETURN_BOOL(_adc->range(min, max));
      _divisor = max - min;
      return _divisor != 0;
    }

    JsonDocument *Mq135::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(2) */
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
        _sensor.set(_value);
      }
      return true;
    }

    Mq135 *useMq135(io::IPin *pin, const char *name) {
      return new Mq135(pin, name);
    }
  }  // namespace dev
}  // namespace esp32m