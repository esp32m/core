#include "esp32m/dev/mwms.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

#include <esp_task_wdt.h>

namespace esp32m {
  namespace dev {

    MicrowaveMotionSensor::MicrowaveMotionSensor(const char *name,
                                                 io::IPin *pin)
        : _name(name) {
      Device::init(Flags::HasSensors);
      _adc = pin ? pin->adc() : nullptr;
      _sampler = new io::Sampler(10, 2);
      xTaskCreate([](void *self) { ((MicrowaveMotionSensor *)self)->run(); },
                  "m/mwms", 4096, this, tskIDLE_PRIORITY + 1, &_task);
    }

    bool MicrowaveMotionSensor::initSensors() {
      if (!_adc)
        return false;
      int min, max;
      ESP_CHECK_RETURN_BOOL(_adc->range(min, max));
      _divisor = max - min;
      return _divisor != 0;
    }

    JsonDocument *MicrowaveMotionSensor::getState(
        RequestContext &ctx) {
      std::lock_guard lock(_sampler->mutex());
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(4) */
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(abs(0.5 - _sampler->avg() / _divisor) * 2);
      arr.add(_sampler->kvar());
      return doc;
    }

    bool MicrowaveMotionSensor::pollSensors() {
      float value;
      {
        std::lock_guard lock(_sampler->mutex());
        value = abs(0.5 - _sampler->avg() / _divisor) * 2;
      }
      if (!isnan(value)) {
        _stamp = millis();
        sensor("value", value);
      }
      return true;
    }

    void MicrowaveMotionSensor::run() {
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        int raw;
        if (_adc && (_adc->read(raw) == ESP_OK))
          _sampler->add(&raw);
        delay(100);
      }
    }

    MicrowaveMotionSensor *useMicrowaveMotionSensor(const char *name,
                                                    io::IPin *pin) {
      return new MicrowaveMotionSensor(name, pin);
    }
  }  // namespace dev
}  // namespace esp32m