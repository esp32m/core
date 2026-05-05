#include "esp32m/dev/pmws.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {

    Pmws::Pmws(uint8_t addr, const char *name, Mode mode)
        : _name(name ? name : (mode == Mode::Pmws ? "PMWS" : "PM")),
          _addr(addr),
          _mode(mode),
          _pm25(this, "pm25"),
          _pm10(this, "pm10"),
          _pm1(this, "pm1") {
      Device::init(Flags::HasSensors);
      _pm25.unit = "µg/m³";
      _pm10.unit = "µg/m³";
      _pm1.unit = "µg/m³";
      if (mode == Mode::Pmws) {
        _temperature = new Sensor(this, "temperature");
        _temperature->precision = 1;
        _temperature->unit = "°C";
        _humidity = new Sensor(this, "humidity");
        _humidity->precision = 1;
        _humidity->unit = "%";
      }
    }

    bool Pmws::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      delay(500);
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      delay(500);
      return mb.isRunning();
    }

    JsonDocument *Pmws::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument();
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_addr);
      arr.add(_pm25.get());
      arr.add(_pm10.get());
      arr.add(_pm1.get());
      if (_mode == Mode::Pmws) {
        arr.add(_temperature->get());
        arr.add(_humidity->get());
      }
      return doc;
    }

    bool Pmws::setConfig(RequestContext &ctx) {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      auto cfg = ctx.data.as<JsonObjectConst>();
      bool changed = false;
      if (_mode == Mode::Pmws) {
        // registers 0x0050..0x0054: temp cal, humid cal, PM10 cal, PM2.5 cal, PM1.0 cal (all ×10)
        uint16_t regs[5] = {};
        auto res =
            mb.request(_addr, modbus::Command::ReadHolding, 0x50, 5, regs);
        if (res != ESP_OK)
          return false;
        auto f = (int16_t)regs[0] / 10.0f;
        if (json::from(cfg["tc"], f, &changed)) {
          int16_t v = (int16_t)(f * 10);
          mb.request(_addr, modbus::Command::WriteRegister, 0x50, 1, &v);
        }
        f = (int16_t)regs[1] / 10.0f;
        if (json::from(cfg["hc"], f, &changed)) {
          int16_t v = (int16_t)(f * 10);
          mb.request(_addr, modbus::Command::WriteRegister, 0x51, 1, &v);
        }
        f = (int16_t)regs[2] / 10.0f;
        if (json::from(cfg["pm10c"], f, &changed)) {
          int16_t v = (int16_t)(f * 10);
          mb.request(_addr, modbus::Command::WriteRegister, 0x52, 1, &v);
        }
        f = (int16_t)regs[3] / 10.0f;
        if (json::from(cfg["pm25c"], f, &changed)) {
          int16_t v = (int16_t)(f * 10);
          mb.request(_addr, modbus::Command::WriteRegister, 0x53, 1, &v);
        }
        f = (int16_t)regs[4] / 10.0f;
        if (json::from(cfg["pm1c"], f, &changed)) {
          int16_t v = (int16_t)(f * 10);
          mb.request(_addr, modbus::Command::WriteRegister, 0x54, 1, &v);
        }
      } else {
        // registers 0x0052..0x0054: PM10 cal, PM2.5 cal, PM1.0 cal (all ×10)
        uint16_t regs[3] = {};
        auto res =
            mb.request(_addr, modbus::Command::ReadHolding, 0x52, 3, regs);
        if (res != ESP_OK)
          return false;
        auto f = (int16_t)regs[0] / 10.0f;
        if (json::from(cfg["pm10c"], f, &changed)) {
          int16_t v = (int16_t)(f * 10);
          mb.request(_addr, modbus::Command::WriteRegister, 0x52, 1, &v);
        }
        f = (int16_t)regs[1] / 10.0f;
        if (json::from(cfg["pm25c"], f, &changed)) {
          int16_t v = (int16_t)(f * 10);
          mb.request(_addr, modbus::Command::WriteRegister, 0x53, 1, &v);
        }
        f = (int16_t)regs[2] / 10.0f;
        if (json::from(cfg["pm1c"], f, &changed)) {
          int16_t v = (int16_t)(f * 10);
          mb.request(_addr, modbus::Command::WriteRegister, 0x54, 1, &v);
        }
      }
      return changed;
    }

    JsonDocument *Pmws::getConfig(RequestContext &ctx) {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return nullptr;
      JsonDocument *doc = new JsonDocument();
      auto root = doc->to<JsonObject>();
      if (_mode == Mode::Pmws) {
        int16_t regs[5] = {};
        auto res =
            mb.request(_addr, modbus::Command::ReadHolding, 0x50, 5, regs);
        if (res != ESP_OK) {
          delete doc;
          return nullptr;
        }
        json::to(root, "tc", regs[0] / 10.0f);
        json::to(root, "hc", regs[1] / 10.0f);
        json::to(root, "pm10c", regs[2] / 10.0f);
        json::to(root, "pm25c", regs[3] / 10.0f);
        json::to(root, "pm1c", regs[4] / 10.0f);
      } else {
        int16_t regs[3] = {};
        auto res =
            mb.request(_addr, modbus::Command::ReadHolding, 0x52, 3, regs);
        if (res != ESP_OK) {
          delete doc;
          return nullptr;
        }
        json::to(root, "pm10c", regs[0] / 10.0f);
        json::to(root, "pm25c", regs[1] / 10.0f);
        json::to(root, "pm1c", regs[2] / 10.0f);
      }
      return doc;
    }

    bool Pmws::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      bool changed = false;
      if (_mode == Mode::Pmws) {
        // reg[0]=humidity×10, reg[1]=temperature×10 (signed), reg[2]=PM2.5, reg[3]=PM10, reg[4]=PM1.0
        uint16_t values[5] = {};
        ESP_CHECK_RETURN_BOOL(
            mb.request(_addr, modbus::Command::ReadHolding, 0x00, 5, values));
        _stamp = millis();
        _humidity->set(values[0] / 10.0f, &changed);
        _temperature->set((int16_t)values[1] / 10.0f, &changed);
        _pm25.set((float)values[2], &changed);
        _pm10.set((float)values[3], &changed);
        _pm1.set((float)values[4], &changed);
      } else {
        // reg[0]=PM2.5, reg[1]=PM10, reg[2]=PM1.0
        uint16_t values[3] = {};
        ESP_CHECK_RETURN_BOOL(
            mb.request(_addr, modbus::Command::ReadHolding, 0x00, 3, values));
        _stamp = millis();
        _pm25.set((float)values[0], &changed);
        _pm10.set((float)values[1], &changed);
        _pm1.set((float)values[2], &changed);
      }
      return true;
    }

    Pmws *usePmws(uint8_t addr, const char *name) {
      return new Pmws(addr, name, Pmws::Mode::Pmws);
    }

    Pmws *usePm(uint8_t addr, const char *name) {
      return new Pmws(addr, name, Pmws::Mode::Pm);
    }

  }  // namespace dev
}  // namespace esp32m
