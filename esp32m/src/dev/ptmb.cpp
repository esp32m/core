#include "esp32m/dev/ptmb.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

#include "math.h"

namespace esp32m {
  namespace dev {
    Ptmb::Ptmb(uint8_t addr, const char *name)
        : _name(name ? name : "PTMB"), _addr(addr), _sensor(this, "pressure") {
      Device::init(Flags::HasSensors);
    }

    bool Ptmb::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      delay(500);
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      delay(500);
      return mb.isRunning();
    }

    JsonDocument *Ptmb::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(3) */
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_addr);
      arr.add(_value);
      return doc;
    }

    bool Ptmb::setConfig(RequestContext &ctx) {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      int16_t regs[5] = {};
      auto res = mb.request(_addr, modbus::Command::ReadHolding, 0x02, 5, regs);
      if (res != ESP_OK)
        return false;
      bool changed = false;
      auto v = regs[0];
      auto cfg=ctx.data.as<JsonObjectConst>();
      if (json::from(cfg["unit"], v, &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x02, 1, &v);
      v = regs[1];
      if (json::from(cfg["decimals"], v, &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x03, 1, &v);
      v = regs[3];
      if (json::from(cfg["zr"], v, &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x05, 1, &v);
      v = regs[4];
      if (json::from(cfg["fp"], v, &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x06, 1, &v);
      return changed;
    }

    JsonDocument *Ptmb::getConfig(RequestContext &ctx) {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return nullptr;
      int16_t regs[3] = {};
      JsonDocument *doc = new JsonDocument(); /* JSON_OBJECT_SIZE(4) */
      auto root = doc->to<JsonObject>();
      auto res = mb.request(_addr, modbus::Command::ReadHolding, 0x02, 3, regs);
      if (res == ESP_OK) {
        json::to(root, "unit", regs[0]);
        json::to(root, "decimals", regs[2]);
      }

      res = mb.request(_addr, modbus::Command::ReadHolding, 0x05, 1, regs);
      if (res == ESP_OK)
        json::to(root, "zr", regs[0]);
      res = mb.request(_addr, modbus::Command::ReadHolding, 0x06, 1, regs);
      if (res == ESP_OK)
        json::to(root, "fp", regs[0]);

      return doc;
    }

    bool Ptmb::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      int16_t values[2] = {};
      ESP_CHECK_RETURN_BOOL(
          mb.request(_addr, modbus::Command::ReadHolding, 0x03, 2, values));
      float divisor;
      switch (values[0]) {
        case 0:
          divisor = 1;
          break;
        case 1:
          divisor = 10;
          break;
        case 2:
          divisor = 100;
          break;
        case 3:
          divisor = 1000;
          break;
        default:
          return true;
      }
      _value = values[1] / divisor;
      _stamp = millis();
      _sensor.set(_value);
      return true;
    }

    Ptmb *usePtmb(uint8_t addr, const char *name) {
      return new Ptmb(addr, name);
    }

  }  // namespace dev
}  // namespace esp32m