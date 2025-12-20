#include "esp32m/dev/ztsyux.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

#include "math.h"

namespace esp32m {
  namespace dev {
    Ztsyux::Ztsyux(uint8_t addr, const char *name)
        : _name(name ? name : "ZTS-YUX"), _addr(addr), _sensor(this, "moisture", "rain") {
      Device::init(Flags::HasSensors);
    }

    bool Ztsyux::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      delay(500);
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      delay(500);
      return mb.isRunning();
    }

    JsonDocument *Ztsyux::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(3) */
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_addr);
      arr.add(_value);
      return doc;
    }

    bool Ztsyux::setConfig(RequestContext &ctx) {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      uint16_t regs[4] = {};
      auto res = mb.request(_addr, modbus::Command::ReadHolding, 0x31, 4, regs);
      if (res != ESP_OK)
        return false;
      auto cfg=ctx.data.as<JsonObjectConst>();
      bool changed = false;
      auto hst = ((float)regs[0]) / 10;
      auto het = ((float)regs[1]) / 10;
      if (json::from(cfg["hst"], hst, &changed)) {
        int16_t v = (int16_t)(hst * 10);
        mb.request(_addr, modbus::Command::WriteRegister, 0x31, 1, &v);
      }
      if (json::from(cfg["het"], het, &changed)) {
        int16_t v = (int16_t)(het * 10);
        mb.request(_addr, modbus::Command::WriteRegister, 0x32, 1, &v);
      }
      if (json::from(cfg["delay"], regs[2], &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x33, 1, &regs[2]);
      if (json::from(cfg["sens"], regs[3], &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x34, 1, &regs[3]);
      return changed;
    }

    JsonDocument *Ztsyux::getConfig(RequestContext &ctx) {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return nullptr;
      int16_t regs[4] = {};
      auto res = mb.request(_addr, modbus::Command::ReadHolding, 0x31, 4, regs);
      if (res != ESP_OK)
        return nullptr;

      JsonDocument *doc = new JsonDocument(); /* JSON_OBJECT_SIZE(4) */
      auto root = doc->to<JsonObject>();
      json::to(root, "hst", ((float)regs[0]) / 10);
      json::to(root, "het", ((float)regs[1]) / 10);
      json::to(root, "delay", regs[2]);
      json::to(root, "sens", regs[3]);
      return doc;
    }

    bool Ztsyux::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      uint16_t value = 0;
      ESP_CHECK_RETURN_BOOL(
          mb.request(_addr, modbus::Command::ReadHolding, 0x00, 1, &value));
      _value = value != 0;
      _stamp = millis();
      _sensor.set(value);
      return true;
    }

    Ztsyux *useZtsyux(uint8_t addr, const char *name) {
      return new Ztsyux(addr, name);
    }

  }  // namespace dev
}  // namespace esp32m