#include "esp32m/dev/soil5.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

#include "math.h"

namespace esp32m {
  namespace dev {
    Soil5::Soil5(uint8_t addr, const char *name)
        : _name(name ? name : "soil5"),
          _addr(addr),
          _moisture(this, "moisture"),
          _temperature(this, "temperature"),
          _conductivity(this, "conductivity"),
          _ph(this, "PH"),
          _nitrogen(this, "nitrogen"),
          _phosphorus(this, "phosphorus"),
          _potassium(this, "potassium"),
          _salinity(this, "salinity"),
          _tds(this, "tds") {
      Device::init(Flags::HasSensors);
      auto group = sensor::nextGroup();
      _moisture.group = group;
      _moisture.precision = 1;
      _temperature.group = group;
      _temperature.precision = 1;
      _conductivity.group = group;
      _ph.group = group;
      _nitrogen.group = group;
      _phosphorus.group = group;
      _potassium.group = group;
      _salinity.group = group;
      _tds.group = group;
    }

    bool Soil5::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      delay(500);
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      delay(500);
      return mb.isRunning();
    }

    JsonDocument *Soil5::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(11) */
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_addr);
      arr.add(_moisture.get());
      arr.add(_temperature.get());
      arr.add(_conductivity.get());
      arr.add(_ph.get());
      arr.add(_nitrogen.get());
      arr.add(_phosphorus.get());
      arr.add(_potassium.get());
      arr.add(_salinity.get());
      arr.add(_tds.get());
      return doc;
    }

    bool Soil5::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      uint16_t values[9] = {};
      ESP_CHECK_RETURN_BOOL(
          mb.request(_addr, modbus::Command::ReadHolding, 0x00, 9, values));
      _stamp = millis();
      bool changed = false;
      _moisture.set(values[0] / 10.0f, &changed);
      _temperature.set(values[1] / 10.0f, &changed);
      _conductivity.set(values[2], &changed);
      _ph.set(values[3] / 10.0f, &changed);
      _nitrogen.set(values[4], &changed);
      _phosphorus.set(values[5], &changed);
      _potassium.set(values[6], &changed);
      _salinity.set(values[7], &changed);
      _tds.set(values[8], &changed);
      if (changed)
        sensor::GroupChanged::publish(_temperature.group);
      return true;
    }

    Soil5 *useSoil5(uint8_t addr, const char *name) {
      return new Soil5(addr, name);
    }

  }  // namespace dev
}  // namespace esp32m