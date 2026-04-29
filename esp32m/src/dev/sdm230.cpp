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

    void Sdm230::setOfflineMeasurements() {
      _v = 0;
      _i = 0;
      _ap = 0;
      _rap = 0;
      _pf = 1;
      _f = 0;
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
      _energyImp.precision = 2;
      _energyImp.setTitle("consumed energy");
      _energyImp.stateClass = StateClass::Total;
      _energyExp.precision = 2;
      _energyExp.setTitle("supplied energy");
      _energyExp.stateClass = StateClass::Total;
      _voltage.precision = 2;
      _current.precision = 2;
      _powerActive.unit = "kW";
      _powerActive.precision = 2;
      _powerActive.stateClass = StateClass::Measurement;
      _powerApparent.unit = "kVA";
      _powerApparent.precision = 2;
      _powerApparent.stateClass = StateClass::Measurement;
      _powerReactive.unit = "kvar";
      _powerReactive.precision = 2;
      _powerReactive.stateClass = StateClass::Measurement;
      _powerFactor.precision = 2;
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
      if (!mb.isRunning()) {
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
        return false;
      }

      {
        float te = _te;
        float ee = _ee;
        float ie = _ie;
        float v = _v;
        float i = _i;
        float ap = _ap;
        float rap = _rap;
        float pf = _pf;
        float f = _f;
        auto fail = [&](esp_err_t err) {
          if (err == ESP_OK)
            return false;
          if (err == ESP_ERR_TIMEOUT) {
            if (_timeoutStreak < std::numeric_limits<uint8_t>::max())
              _timeoutStreak++;
            if (_timeoutStreak >= OfflineTimeoutThreshold)
              setOfflineMeasurements();
          } else {
            _timeoutStreak = 0;
          }
          return true;
        };

        if (fail(read(Register::Voltage, v))) {
        } else if (fail(read(Register::Current, i))) {
        } else if (fail(read(Register::Power, ap))) {
        } else if (fail(read(Register::ReactivePower, rap))) {
        } else if (fail(read(Register::PowerFactor, pf))) {
        } else if (fail(read(Register::Frequency, f))) {
        } else if (fail(read(Register::ImpActiveEnergy, ie))) {
        } else if (fail(read(Register::ExpActiveEnergy, ee))) {
        } else if (fail(read(Register::TotalActiveEnergy, te))) {
        } else {
          _timeoutStreak = 0;
          _te = te;
          _ee = ee;
          _ie = ie;
          _v = v;
          _i = i;
          _ap = ap;
          _rap = rap;
          _pf = pf;
          _f = f;
          _stamp = millis();
        }
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
      return true;
    }

    Sdm230* useSdm230(uint8_t addr) {
      return new Sdm230(addr);
    }

  }  // namespace dev
}  // namespace esp32m