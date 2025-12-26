#include "esp32m/dev/flow.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

#include <math.h>
#include <limits>

namespace esp32m {
  namespace dev {

    FlowSensor::FlowSensor(const char *name, io::IPin *pin)
        : _name(name),
          _sensorFlow(this, "volume_flow_rate", "flow"),
          _sensorConsumption(this, "water", "consumption") {
      auto group = sensor::nextGroup();
      _sensorConsumption.group = group;
      _sensorConsumption.stateClass = StateClass::Total;
      _sensorConsumption.unit = "m³";
      _sensorFlow.group = group;
      _sensorFlow.unit = "m³/h";
      _sensorFlow.stateClass = StateClass::Measurement;
      Device::init(Flags::HasSensors);
      _pin = pin;
    }

    bool FlowSensor::initSensors() {
      if (!_pin)
        return false;
      auto pcnt = _pin->pcnt();
      if (!pcnt)
        return false;
      ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt->setEdgeAction(
          PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
      ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt->setFilter(12 * 1000));  // 12 us is as high as it can go
      if (!pcnt->isEnabled())
        ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt->enable(true));
      return pcnt->isEnabled();
    }

    JsonDocument *FlowSensor::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(3) */
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
        _sensorFlow.set(_value);
        _sensorConsumption.set(_consumption);
        if (ms - _lastDump > 10000 &&
            abs(_consumption - _dumpedConsumption) >
                std::numeric_limits<float>::epsilon()) {
          config::Changed::publish(this);
          _dumpedConsumption = _consumption;
          _lastDump = ms;
        }
      }
      return true;
    }

    bool FlowSensor::setConfig(RequestContext &ctx) {
      bool changed = false;
      json::from(ctx.data["consumption"], _consumption, &changed);
      return changed;
    }

    JsonDocument *FlowSensor::getConfig(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_OBJECT_SIZE(1) */
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