#include "esp32m/dev/sm538x.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {
    const char *DIR = "direction";
    const char *SPEED = "speed";

    Sm538x::Sm538x(uint8_t addr) : _addr(addr) {
      Device::init(Flags::HasSensors);
    }

    bool Sm538x::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        ESP_CHECK_RETURN_BOOL(mb.start());
      delay(100);
      uint16_t reg[0x6];
      ESP_CHECK_RETURN_BOOL(
          mb.request(_addr, modbus::Command::ReadHolding, 0x64, 0x6, &reg));
      _model = reg[0];
      logI(
          "found wind sensor, model %i, points: %i, id: %i, baud: %i, mode: "
          "%i, "
          "protocol %i",
          _model, reg[1], reg[2], reg[3], reg[4], reg[5]);
      _props["model"] = _model;
      sprintf(_name, "SM%i", _model);
      return true;
    }

    DynamicJsonDocument *Sm538x::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(4));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_addr);
      arr.add(_model);
      arr.add(_value);
      return doc;
    }

    bool Sm538x::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      delay(100);
      uint16_t reg;
      ESP_CHECK_RETURN_BOOL(
          mb.request(_addr, modbus::Command::ReadHolding, 0x00, 0x1, &reg));
      const char *name = nullptr;
      switch (_model) {
        case 5387:  // wind direction, degrees
          _value = reg / 100.0;
          name = DIR;
          break;
        case 5386:  // wind speed
          _value = reg / 10.0;
          name = SPEED;
          break;
        default:
          logI("unknown device model: %i", _model);
          break;
      }
      if (name && !isnan(_value)) {
        _stamp = millis();
        sensor(name, _value);
      }
      return true;
    }

    void useSm538x(uint8_t addr) {
      new Sm538x(addr);
    }
  }  // namespace dev
}  // namespace esp32m