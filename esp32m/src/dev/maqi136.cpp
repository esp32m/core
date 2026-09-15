#include "esp32m/dev/maqi136.hpp"
#include <math.h>

#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {
    namespace {
      // Conventional metre of water: 1 mH2O = 9806.65 Pa.
      constexpr float PaPerMeterH2O = 9806.65f;
      constexpr float PaPerPsi = 6894.757293168f;
    }  // namespace

    Maqi136::Maqi136(uint8_t addr)
        : _addr(addr), _sensor(this, "distance", "level") {
      _sensor.unit = "m";
      _sensor.precision = _precision;
      _sensor.stateClass = StateClass::Measurement;
      Device::init(Flags::HasSensors);
    }

    void Maqi136::calibrate(float multiplier, float offset) {
      _calibrationMultiplier = multiplier;
      _calibrationOffset = offset;
      _calibrated = true;
    }

    void Maqi136::setRange(float min, float max) {
      _rangeMin = min;
      _rangeMax = max;
      _rangeOverridden = true;
    }

    bool Maqi136::metersPerUnit(Units units, float &factor) {
      switch (units) {
        case Units::MPa:
          factor = 1000000.0f / PaPerMeterH2O;
          return true;
        case Units::KPa:
          factor = 1000.0f / PaPerMeterH2O;
          return true;
        case Units::Pa:
          factor = 1.0f / PaPerMeterH2O;
          return true;
        case Units::Bar:
          factor = 100000.0f / PaPerMeterH2O;
          return true;
        case Units::Mbar:
          factor = 100.0f / PaPerMeterH2O;
          return true;
        case Units::Psi:
          factor = PaPerPsi / PaPerMeterH2O;
          return true;
        case Units::MH2O:
          factor = 1.0f;
          return true;
        case Units::MmH2O:
          factor = 0.001f;
          return true;
        default:
          return false;
      }
    }

    uint8_t Maqi136::precisionForStep(float step) {
      step = fabsf(step);
      uint8_t precision = 0;
      while (step > 0 && step < 1 && precision < 6) {
        step *= 10;
        precision++;
      }
      return precision;
    }

    void Maqi136::invalidate() {
      _stamp = 0;
      _sensor.resetState();
    }

    bool Maqi136::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      delay(500);
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      delay(500);
      return mb.isRunning();
    }

    JsonDocument *Maqi136::getState(RequestContext &ctx) {
      auto regc = sizeof(_regs) / sizeof(uint16_t);
      JsonDocument *doc = new JsonDocument();
      JsonArray arr = doc->to<JsonArray>();
      if (_stamp)
        arr.add(millis() - _stamp);
      else
        arr.add(-1);
      arr.add(_addr);
      arr.add(_units);
      arr.add(_decimals);
      arr.add(_precision);
      arr.add(_min);
      arr.add(_max);
      arr.add(_value);
      arr.add(_offset);
      auto regs = arr.add<JsonArray>();
      for (auto i = 0; i < regc; i++) regs.add(_regs[i]);
      arr.add(_calibrated);
      arr.add(_calibrationMultiplier);
      arr.add(_calibrationOffset);
      return doc;
    }

    bool Maqi136::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning()) {
        invalidate();
        return false;
      }
      auto err = mb.request(_addr, modbus::Command::ReadHolding, 0,
                            sizeof(_regs) / sizeof(uint16_t), &_regs);
      if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(err);
        invalidate();
        return false;
      }

      auto units = static_cast<Units>(_regs[2]);
      auto decimals = _regs[3];
      float factor;
      if (decimals > 3) {
        logW("invalid decimal count: %u", (unsigned)decimals);
        invalidate();
        return false;
      }
      if (!metersPerUnit(units, factor)) {
        logW("unit code %u cannot be converted to water level",
             (unsigned)_regs[2]);
        invalidate();
        return false;
      }
      auto rawMeasurement = static_cast<int16_t>(_regs[4]);
      if (rawMeasurement < 0) {
        logW("negative calibrated measurement: %d", (int)rawMeasurement);
        invalidate();
        return false;
      }

      constexpr float divisors[] = {1.0f, 10.0f, 100.0f, 1000.0f};
      auto divisor = divisors[decimals];
      auto toMeters = [divisor, factor](uint16_t raw) {
        return static_cast<int16_t>(raw) / divisor * factor;
      };
      _units = _regs[2];
      _decimals = decimals;
      _precision = precisionForStep(
          _calibrated ? _calibrationMultiplier : factor / divisor);
      if (_rangeOverridden) {
        _min = _rangeMin;
        _max = _rangeMax;
      } else {
        _min = toMeters(_regs[5]);
        _max = toMeters(_regs[6]);
      }
      if (_calibrated) {
        _value = rawMeasurement > 0
                     ? rawMeasurement * _calibrationMultiplier +
                           _calibrationOffset
                     : 0.0f;
        _offset = static_cast<int16_t>(_regs[7]) * _calibrationMultiplier;
      } else {
        _value = toMeters(_regs[4]);
        _offset = toMeters(_regs[7]);
      }
      _stamp = millis();
      _sensor.precision = _precision;
      _sensor.set(_value);
      return true;
    }

    Maqi136 *useMaqi136(uint8_t addr) {
      return new Maqi136(addr);
    }

  }  // namespace dev
}  // namespace esp32m
