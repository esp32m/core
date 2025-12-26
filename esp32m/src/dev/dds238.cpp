#include "esp32m/dev/dds238.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {
    Dds238::Dds238(uint8_t addr)
        : _addr(addr),
          _energyImp(this, "energy", "energy_imp"),
          _energyExp(this, "energy", "energy_exp"),
          _voltage(this, "voltage"),
          _current(this, "current"),
          _powerActive(this, "power"),
          _powerApparent(this, "apparent_power"),
          _powerReactive(this, "reactive_power"),
          _powerFactor(this, "power_factor"),
          _frequency(this, "frequency") {
      Device::init(Flags::HasSensors);
      auto group = sensor::nextGroup();
      _energyImp.group = group;
      _energyImp.precision = 2;
      _energyImp.setTitle("consumed energy");
      _energyImp.stateClass = StateClass::Total;
      _energyExp.group = group;
      _energyExp.precision = 2;
      _energyExp.setTitle("supplied energy");
      _energyExp.stateClass = StateClass::Total;
      _voltage.group = group;
      _voltage.precision = 2;
      _current.group = group;
      _current.precision = 2;
      _powerActive.group = group;
      _powerActive.unit = "kW";
      _powerActive.precision = 2;
      _powerActive.stateClass = StateClass::Measurement;
      _powerApparent.group = group;
      _powerApparent.unit = "kVA";
      _powerApparent.precision = 2;
      _powerApparent.stateClass = StateClass::Measurement;
      _powerReactive.group = group;
      _powerReactive.unit = "kvar";
      _powerReactive.precision = 2;
      _powerReactive.stateClass = StateClass::Measurement;
      _powerFactor.group = group;
      _powerFactor.precision = 2;
      _frequency.group = group;
      _frequency.precision = 2;
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

    JsonDocument *Dds238::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(11) */
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
      _stamp = millis();

      bool changed = false;
      _energyExp.set(_ee, &changed);
      _energyImp.set(_ie, &changed);
      _voltage.set(_v, &changed);
      _current.set(_i, &changed);
      _powerActive.set(_ap, &changed);
      _powerApparent.set(_ap/_pf, &changed);
      _powerReactive.set(_rap, &changed);
      _powerFactor.set(_pf, &changed);
      _frequency.set(_f, &changed);
      if (changed)
        sensor::GroupChanged::publish(_energyExp.group);

      return true;
    }


    Dds238* useDds238(uint8_t addr) {
      return new Dds238(addr);
    }

  }  // namespace dev
}  // namespace esp32m