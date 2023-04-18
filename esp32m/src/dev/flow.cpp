#include "esp32m/dev/flow.hpp"
#include "esp32m/base.hpp"
#include "esp32m/config/changed.hpp"
#include "esp32m/defs.hpp"

#include <math.h>
#include <limits>

namespace esp32m {
  namespace dev {

    FlowSensor::FlowSensor(const char *name, io::IPin *pin)
        : Device(Flags::HasSensors), _name(name) {
      _pin = pin;
    }

    bool FlowSensor::initSensors() {
      return _pin && _pin->pcnt();
    }

    DynamicJsonDocument *FlowSensor::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(3));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_value);
      arr.add(_consumption);
      return doc;
    }

    bool FlowSensor::pollSensors() {
      auto pcnt = _pin->pcnt();
      if (!pcnt)
        return false;
      int pc;
      auto err = ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt->read(pc, true));
      if (err == ESP_OK) {
        auto ms = millis();
        auto passed = ms - _stamp;
        _value = compute(pc, passed);
        _consumption += _value * passed / 1000 / 60;
        _stamp = ms;
        sensor("flow", _value);
        sensor("consumption", _consumption);
        if (ms - _lastDump > 10000 &&
            abs(_consumption - _dumpedConsumption) >
                std::numeric_limits<float>::epsilon()) {
          EventConfigChanged::publish(this);
          _dumpedConsumption = _consumption;
          _lastDump = ms;
        }
      }
      return true;
    }

    bool FlowSensor::setConfig(const JsonVariantConst cfg,
                               DynamicJsonDocument **result) {
      bool changed = false;
      json::from(cfg["consumption"], _consumption, &changed);
      return changed;
    }

    DynamicJsonDocument *FlowSensor::getConfig(RequestContext &ctx) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
      auto root = doc->to<JsonObject>();
      root["consumption"] = _consumption;
      return doc;
    }

    float FlowSensor::compute(int pc, int ms) {
      if (pc == 0)
        return 0;
      float hz = (float)pc * 1000 / ms;
      return (hz + 8) / 6;
    }

    FlowSensor *useFlowSensor(const char *name, io::IPin *pin) {
      return new FlowSensor(name, pin);
    }
  }  // namespace dev
}  // namespace esp32m