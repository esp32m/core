#include "esp32m/dev/sdm230.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {

    esp_err_t Sdm230::read(Register reg, float& res) {
      modbus::Master& mb = modbus::Master::instance();
      uint16_t hex[2] = {};
      // unfortunately, reading in bulk is not supported, at least not by
      // SDM230
      ESP_CHECK_RETURN(
          mb.request(_addr, modbus::Command::ReadInput, reg, 2, hex));
      ((uint16_t*)&res)[0] = hex[1];
      ((uint16_t*)&res)[1] = hex[0];
      return ESP_OK;
    }

    Sdm230::Sdm230(uint8_t addr)
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

    bool Sdm230::initSensors() {
      modbus::Master& mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      return mb.isRunning();
    }

    JsonDocument* Sdm230::getState(RequestContext& ctx) {
      JsonDocument* doc = new JsonDocument();
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

    bool Sdm230::pollSensors() {
      modbus::Master& mb = modbus::Master::instance();
      bool ok = false;
      if (mb.isRunning()) {
        if (read(Register::Voltage, _v) != ESP_OK)
          _v = 0;
        else
          ok = true;
        if (read(Register::Current, _i) != ESP_OK)
          _i = 0;
        if (read(Register::Power, _ap) != ESP_OK)
          _ap = 0;
        if (read(Register::ReactivePower, _rap) != ESP_OK)
          _rap = 0;
        if (read(Register::PowerFactor, _pf) != ESP_OK)
          _pf = 1;
        if (read(Register::Frequency, _f) != ESP_OK)
          _f = 0;
        read(Register::ImpActiveEnergy, _ie);
        read(Register::ExpActiveEnergy, _ee);
        read(Register::TotalActiveEnergy, _te);
        if (ok)
          _stamp = millis();
      }
      bool changed = false;
      _energyExp.set(_ee, &changed);
      _energyImp.set(_ie, &changed);
      _voltage.set(_v, &changed);
      _current.set(_i, &changed);
      _powerActive.set(_ap, &changed);
      _powerApparent.set(_ap / _pf, &changed);
      _powerReactive.set(_rap, &changed);
      _powerFactor.set(_pf, &changed);
      _frequency.set(_f, &changed);
      return ok;
    }

    Sdm230* useSdm230(uint8_t addr) {
      return new Sdm230(addr);
    }

  }  // namespace dev
}  // namespace esp32m