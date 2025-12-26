#include <math.h>

#include "esp32m/dev/ina219.hpp"

namespace esp32m {
  namespace ina219 {
    static const int ShiftRst = 15;
    static const int ShiftBrng = 13;
    static const int ShiftPg0 = 11;
    static const int ShiftBadc0 = 7;
    static const int ShiftSadc0 = 3;
    static const int ShiftMode = 0;

    static const uint16_t MaskPg = (3 << ShiftPg0);
    static const uint16_t MaskBadc = (0x0f << ShiftBadc0);
    static const uint16_t MaskSadc = (0x0f << ShiftSadc0);
    static const uint16_t MaskMode = (7 << ShiftMode);
    static const uint16_t MaskBrng = (1 << ShiftBrng);

    static const float ShuntMaxU[] = {
        [Gain::x1] = 0.04,
        [Gain::x05] = 0.08,
        [Gain::x025] = 0.16,
        [Gain::x0125] = 0.32,
    };

    void Settings::setRange(BusRange value) {
      uint16_t c = (_config & ~MaskBrng) | (value << ShiftBrng);
      if (c == _config)
        return;
      _config = c;
      _modified = true;
    }
    BusRange Settings::getRange() const {
      return (BusRange)((_config & MaskBrng) >> ShiftBrng);
    }
    void Settings::setGain(Gain value) {
      uint16_t c = (_config & ~MaskPg) | (value << ShiftPg0);
      if (c == _config)
        return;
      _config = c;
      _modified = true;
    }
    Gain Settings::getGain() const {
      return (Gain)((_config & MaskPg) >> ShiftPg0);
    }
    void Settings::setURes(Resolution value) {
      uint16_t c = (_config & ~MaskBadc) | (value << ShiftBadc0);
      if (c == _config)
        return;
      _config = c;
      _modified = true;
    }
    Resolution Settings::getURes() const {
      return (Resolution)((_config & MaskBadc) >> ShiftBadc0);
    }
    void Settings::setIRes(Resolution value) {
      uint16_t c = (_config & ~MaskSadc) | (value << ShiftSadc0);
      if (c == _config)
        return;
      _config = c;
      _modified = true;
    }
    Resolution Settings::getIRes() const {
      return (Resolution)((_config & MaskSadc) >> ShiftSadc0);
    }
    void Settings::setMode(Mode value) {
      uint16_t c = (_config & ~MaskMode) | (value << ShiftMode);
      if (c == _config)
        return;
      _config = c;
      _modified = true;
    }
    Mode Settings::getMode() const {
      return (Mode)((_config & MaskMode) >> ShiftMode);
    }
    void Settings::setIMax(float value) {
      if (value == _imax)
        return;
      _imax = value;
      _modified = true;
    }
    float Settings::getIMax() const {
      return _imax;
    }
    void Settings::setShunt(float value) {
      uint16_t v = (uint16_t)(value * 1000);
      if (v == _shunt)
        return;
      _shunt = v;
      _modified = true;
    }
    float Settings::getShunt() const {
      return _shunt * 0.001;
    }

    Core::Core(i2c::MasterDev* i2c) : _i2c(i2c) {}

    esp_err_t Core::sync(bool force) {
      if (!_settings._modified && !force)
        return ESP_OK;
      uint16_t config = 0;
      ESP_CHECK_RETURN(_i2c->read(Register::Config, config));
      if (_settings._config != config) {
        ESP_CHECK_LOGW_RETURN(_i2c->write(Register::Config, _settings._config),
                              "failed to write config");
        ESP_CHECK_RETURN(_i2c->read(Register::Config, config));
        _settings._config = config;
      }
      float shunt = _settings.getShunt();
      _lsbI = (uint16_t)(ShuntMaxU[_settings.getGain()] / shunt / 32767 *
                         100000000);
      _lsbI /= 100000000;
      _lsbI /= 0.0001;
      _lsbI = ceil(_lsbI);
      _lsbI *= 0.0001;

      _lsbP = _lsbI * 20;

      uint16_t cal = (uint16_t)((0.04096) / (_lsbI * shunt / 1000.0));
      ESP_CHECK_LOGW_RETURN(_i2c->write(Register::Calibration, cal),
                            "failed to calibrate");
      _settings._modified = false;
      return ESP_OK;
    }

    esp_err_t Core::reset() {
      ESP_CHECK_RETURN(_i2c->write(Register::Config, 1 << ShiftRst));
      ESP_CHECK_RETURN(_i2c->read(Register::Config, _settings._config));
      return ESP_OK;
    }

    esp_err_t Core::measure() {
      Mode mode = _settings.getMode();
      if (mode < Mode::TrigShunt || mode > Mode::TrigShuntBus) {
        logW("could not trigger conversion in this mode: %d", mode);
        return ESP_ERR_INVALID_STATE;
      }
      return _i2c->write(Register::Config, _settings._config);
    }

    esp_err_t Core::getBusVoltage(float& value) {
      ESP_CHECK_RETURN(sync());
      int16_t raw = 0;
      ESP_CHECK_RETURN(_i2c->read(Register::Bus, raw));
      value = (raw >> 3) * 0.004;
      return ESP_OK;
    }

    esp_err_t Core::getShuntVoltage(float& value) {
      ESP_CHECK_RETURN(sync());
      int16_t raw = 0;
      ESP_CHECK_RETURN(_i2c->read(Register::Shunt, raw));
      value = raw / 100000.0;
      return ESP_OK;
    }

    esp_err_t Core::getCurrent(float& value) {
      ESP_CHECK_RETURN(sync());
      int16_t raw = 0;
      ESP_CHECK_RETURN(_i2c->read(Register::Current, raw));
      value = raw * _lsbI;
      return ESP_OK;
    }

    esp_err_t Core::getPower(float& value) {
      ESP_CHECK_RETURN(sync());
      int16_t raw = 0;
      ESP_CHECK_RETURN(_i2c->read(Register::Power, raw));
      value = raw * _lsbP;
      return ESP_OK;
    }
  }  // namespace ina219

  namespace dev {

    Ina219::Ina219(i2c::MasterDev* i2c)
        : ina219::Core(i2c),
          _voltage(this, "voltage"),
          _current(this, "current"),
          _power(this, "power") {
      Device::init(Flags::HasSensors);
      auto group = sensor::nextGroup();
      _voltage.group = group;
      _voltage.precision = 2;
      _current.group = group;
      _current.precision = 4;
      _power.group = group;
      _power.precision = 4;
      _power.unit = "W";
    }

    bool Ina219::initSensors() {
      return sync(true) == ESP_OK;
    }

    bool Ina219::pollSensors() {
      float value;
      bool changed = false;
      ESP_CHECK_RETURN_BOOL(getBusVoltage(value));
      _voltage.set(value, &changed);
      //      ESP_CHECK_RETURN_BOOL(getShuntVoltage(value));
      ESP_CHECK_RETURN_BOOL(getCurrent(value));
      _current.set(value, &changed);
      ESP_CHECK_RETURN_BOOL(getPower(value));
      _power.set(value, &changed);
      if (changed)
        sensor::GroupChanged::publish(_voltage.group);
      return true;
    }

    JsonDocument* Ina219::getState(RequestContext& ctx) {
      JsonDocument* doc = new JsonDocument(); /* JSON_OBJECT_SIZE(5) */
      JsonObject root = doc->to<JsonObject>();
      float value;
      root["addr"] = _i2c->address();
      if (getBusVoltage(value) == ESP_OK)
        root["voltage"] = value;
      if (getShuntVoltage(value) == ESP_OK)
        root["shuntVoltage"] = value;
      if (getCurrent(value) == ESP_OK)
        root["current"] = value;
      if (getPower(value) == ESP_OK)
        root["power"] = value;
      return doc;
    }

    void useIna219(uint8_t addr) {
      new Ina219(i2c::MasterDev::create(addr));
    }
  }  // namespace dev
}  // namespace esp32m