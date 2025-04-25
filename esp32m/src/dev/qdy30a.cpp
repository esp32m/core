#include "esp32m/dev/qdy30a.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

#include "math.h"

namespace esp32m {
  namespace dev {
    Qdy30a::Qdy30a(uint8_t addr)
        : _addr(addr), _sensor(this, "distance", "level") {
      _sensor.unit = "cm";
      _sensor.precision = 1;
      Device::init(Flags::HasSensors);
    }

    bool Qdy30a::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      delay(500);
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      delay(500);
      return mb.isRunning();
    }

    JsonDocument *Qdy30a::getState(RequestContext &ctx) {
      auto regc = sizeof(_regs) / sizeof(uint16_t);
      JsonDocument *doc =
          new JsonDocument(); /* JSON_ARRAY_SIZE(8 + regc) */
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_addr);
      arr.add(_units);
      arr.add(_decimals);
      arr.add(_min);
      arr.add(_max);
      arr.add(_value);
      auto regs = arr.add<JsonArray>();
      for (auto i = 0; i < regc; i++) regs.add(_regs[i]);
      return doc;
    }

    bool Qdy30a::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      ESP_CHECK_RETURN_BOOL(mb.request(_addr, modbus::Command::ReadHolding, 0,
                                       sizeof(_regs) / sizeof(uint16_t),
                                       &_regs));
      _units = _regs[2];
      _decimals = _regs[3];
      float div = pow(10, _regs[3]);
      _value = ((int16_t)_regs[4]) / div;
      _min = ((int16_t)_regs[5]) / div;
      _max = ((int16_t)_regs[6]) / div;
      _stamp = millis();
      _sensor.set(_value);
//      delay(500);
      return true;
    }

    Qdy30a *useQdy30a(uint8_t addr) {
      return new Qdy30a(addr);
    }

  }  // namespace dev
}  // namespace esp32m