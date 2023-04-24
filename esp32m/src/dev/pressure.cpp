#include "esp32m/dev/pressure.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {

    PressureSensor::PressureSensor(const char *name, io::IPin *pin)
        : _name(name) {
      Device::init(Flags::HasSensors);
      _adc = pin ? pin->adc() : nullptr;
    }

    bool PressureSensor::initSensors() {
      if (!_adc)
        return false;
      int min, max;
      ESP_CHECK_RETURN_BOOL(_adc->range(min, max));
      _divisor = max - min;
      return _divisor != 0;
    }

    DynamicJsonDocument *PressureSensor::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(4));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_value);
      arr.add(_mv * _vdiv / 1000.0);
      arr.add(_raw);
      return doc;
    }

    bool PressureSensor::pollSensors() {
      if (!_adc)
        return false;
      int raw;
      uint32_t mv;
      auto err = ESP_ERROR_CHECK_WITHOUT_ABORT(_adc->read(raw, &mv));
      if (err == ESP_OK) {
        _value = compute(mv, (float)raw / _divisor);
        _raw = raw;
        _mv = mv;
        _stamp = millis();
        sensor("pressure", _value);
        sensor("voltage", _mv * _vdiv / 1000.0);
      }
      return true;
    }

    float PressureSensor::compute(uint32_t mv, float r) {
      const float vcc = 5000;
      float vout = mv * _vdiv;
      float pmpa = (vout / vcc - 0.1) / _slope;
      return pmpa < 0 ? 0 : pmpa / 0.101325;
    }

    PressureSensor *usePressureSensor(const char *name, io::IPin *pin) {
      return new PressureSensor(name, pin);
    }
  }  // namespace dev
}  // namespace esp32m