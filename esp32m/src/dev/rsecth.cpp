#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/dev/rsecth.hpp"

#include "math.h"

namespace esp32m {
  namespace dev {
    Rsecth::Rsecth(uint8_t addr, const char *name)
        : _addr(addr), _moisture(this, "moisture"), _temperature(this, "temperature"), _conductivity(this, "conductivity"), _salinity(this, "salinity"), _tds(this, "tds") {
      _name = name ? name : "RS-ECTH";
      Device::init(Flags::HasSensors);
      auto group = sensor::nextGroup();
      _moisture.group = group;
      _moisture.precision = 1;
      _temperature.group = group;
      _temperature.precision = 1;
      _conductivity.group = group;
      _salinity.group = group;
      _tds.group = group;
    }

    bool Rsecth::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      delay(500);
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      delay(500);
      return mb.isRunning();
    }

    DynamicJsonDocument *Rsecth::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(7));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_addr);
      arr.add(_moisture.get());
      arr.add(_temperature.get());
      arr.add(_conductivity.get());
      arr.add(_salinity.get());
      arr.add(_tds.get());
      return doc;
    }

    bool Rsecth::setConfig(const JsonVariantConst cfg,
                           DynamicJsonDocument **result) {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      uint16_t regs[3] = {};
      auto res = mb.request(_addr, modbus::Command::ReadHolding, 0x22, 3, regs);
      if (res != ESP_OK)
        return false;
      bool changed = false;
      auto f = ((float)regs[0]) / 10;
      if (json::from(cfg["ctc"], f, &changed)) {
        int16_t v = (int16_t)(f * 10);
        mb.request(_addr, modbus::Command::WriteRegister, 0x22, 1, &v);
      }
      f = ((float)regs[1]) / 100;
      if (json::from(cfg["sc"], f, &changed)) {
        int16_t v = (int16_t)(f * 100);
        mb.request(_addr, modbus::Command::WriteRegister, 0x23, 1, &v);
      }
      f = ((float)regs[2]) / 100;
      if (json::from(cfg["tdsc"], f, &changed)) {
        int16_t v = (int16_t)(f * 100);
        mb.request(_addr, modbus::Command::WriteRegister, 0x24, 1, &v);
      }
      res = mb.request(_addr, modbus::Command::ReadHolding, 0x50, 3, regs);
      if (res == ESP_OK) {
        f = regs[0] / 10.0f;
        if (json::from(cfg["tcv"], f, &changed)) {
          int16_t v = (int16_t)(f * 10);
          mb.request(_addr, modbus::Command::WriteRegister, 0x50, 1, &v);
        }
        f = regs[1] / 10.0f;
        if (json::from(cfg["mcc"], f, &changed)) {
          int16_t v = (int16_t)(f * 10);
          mb.request(_addr, modbus::Command::WriteRegister, 0x51, 1, &v);
        }
        if (json::from(cfg["ccv"], regs[2], &changed))
          mb.request(_addr, modbus::Command::WriteRegister, 0x52, 1, &regs[2]);
      }
      return changed;
    }

    DynamicJsonDocument *Rsecth::getConfig(RequestContext &ctx) {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return nullptr;
      int16_t regs[3] = {};
      auto res = mb.request(_addr, modbus::Command::ReadHolding, 0x22, 3, regs);
      if (res != ESP_OK)
        return nullptr;

      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(6));
      auto root = doc->to<JsonObject>();
      json::to(root, "ctc", ((float)regs[0]) / 10);
      json::to(root, "sc", ((float)regs[1]) / 100);
      json::to(root, "tdsc", regs[2] / 100.0f);

      res = mb.request(_addr, modbus::Command::ReadHolding, 0x50, 3, regs);
      if (res == ESP_OK) {
        json::to(root, "tcv", regs[0] / 10.0f);
        json::to(root, "mcc", regs[1] / 10.0f);
        json::to(root, "ccv", regs[2]);
      }
      return doc;
    }

    bool Rsecth::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      uint16_t values[5] = {};
      ESP_CHECK_RETURN_BOOL(
          mb.request(_addr, modbus::Command::ReadHolding, 0x00, 5, values));
      _stamp = millis();
      bool changed=false;
      _moisture.set(values[0] / 10.0f, &changed);
      _temperature.set(values[1] / 10.0f, &changed);
      _conductivity.set(values[2], &changed);
      _salinity.set(values[3], &changed);
      _tds.set(values[4], &changed);
      if (changed)
        sensor::GroupChanged::publish(_temperature.group);
      return true;
    }

    Rsecth *useRsecth(uint8_t addr, const char *name) {
      return new Rsecth(addr, name);
    }

  }  // namespace dev
}  // namespace esp32m