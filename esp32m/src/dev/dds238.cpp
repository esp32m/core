#include "esp32m/dev/dds238.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {
    Dds238::Dds238(uint8_t addr): _addr(addr) {
      Device::init(Flags::HasSensors); 
    }

    float readFloat(uint16_t *ptr) {
      return (float)((ptr[0] << 16) | ptr[1]) / 100.0;
    }

    bool Dds238::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      return mb.isRunning();
    }

    DynamicJsonDocument *Dds238::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(11));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_addr);
      arr.add(_te);
      arr.add(_ee);
      arr.add(_ie);
      arr.add(_v);
      arr.add(_i);
      arr.add(_ap);
      arr.add(_rap);
      arr.add(_pf);
      arr.add(_f);
      return doc;
    }

    bool Dds238::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      uint16_t reg[0x18];
      memset(reg, 0, sizeof(reg));
      ESP_CHECK_RETURN_BOOL(
          mb.request(_addr, modbus::Command::ReadHolding, 0, 0x18, &reg));
      _te = readFloat(reg + 0x0);
      _ee = readFloat(reg + 0x8);
      _ie = readFloat(reg + 0xA);
      _v = (float)reg[0xC] / 10;
      _i = (float)reg[0xD] / 100;
      _ap = (float)(int16_t)reg[0xE] / 1000;
      _rap = (float)(int16_t)reg[0xF] / 1000;
      _pf = (float)reg[0x10] / 1000;
      _f = (float)reg[0x11] / 100;
      sensor("voltage", _v);
      sensor("current", _i);
      sensor("frequency", _f);
      sensor("power-active", _ap);
      sensor("power-reactive", _rap);
      _stamp = millis();
      return true;
    }

    void useDds238(uint8_t addr) {
      new Dds238(addr);
    }

  }  // namespace dev
}  // namespace esp32m