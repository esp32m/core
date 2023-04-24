#include "esp32m/dev/moisture.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {

    MoistureSensor::MoistureSensor(const char *name, io::IPin *pin)
        :  _name(name) {
      Device::init(Flags::HasSensors);
      _adc = pin ? pin->adc() : nullptr;
    }

    bool MoistureSensor::initSensors() {
      if (!_adc) {
        logW("could not initialize ADC pin");
        return false;
      }
      int min, max;
      ESP_CHECK_RETURN_BOOL(_adc->range(min, max));
      _divisor = max - min;
      return _divisor != 0;
    }

    DynamicJsonDocument *MoistureSensor::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(3));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_value);
      arr.add(_mv);
      return doc;
    }

    bool MoistureSensor::pollSensors() {
      if (!_adc)
        return false;
      int raw;
      auto err = ESP_ERROR_CHECK_WITHOUT_ABORT(_adc->read(raw, &_mv));
      if (err == ESP_OK) {
        _value = 1.0 - ((float)raw / _divisor);
        _stamp = millis();
        sensor("value", _value);
      }
      return true;
    }

    MoistureSensor *useMoistureSensor(const char *name, io::IPin *pin) {
      return new MoistureSensor(name, pin);
    }
  }  // namespace dev
}  // namespace esp32m