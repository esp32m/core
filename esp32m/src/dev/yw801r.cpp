#include "esp32m/dev/yw801r.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {
    Yw801r::Yw801r(uint8_t addr) : _addr(addr), _sensor(this, "distance", "level") {
      _sensor.unit = "cm";
      _sensor.precision = 1;
      Device::init(Flags::HasSensors);
    }

    bool Yw801r::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      delay(500);
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      delay(500);
      return mb.isRunning();
    }

    JsonDocument *Yw801r::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(4) */
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_addr);
      arr.add(_pv);
      arr.add(_ad);
      return doc;
    }

    bool Yw801r::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      if (millis() - _stamp < 2000)
        return true;
      ESP_CHECK_RETURN_BOOL(
          mb.request(_addr, modbus::Command::ReadHolding, 1, 1, &_pv));
      _sensor.set(_pv);
      _stamp = millis();
      delay(500);
      return true;
    }

    Yw801r *useYw801r(uint8_t addr) {
      return new Yw801r(addr);
    }

  }  // namespace dev
}  // namespace esp32m