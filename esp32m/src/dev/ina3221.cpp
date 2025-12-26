#include <math.h>

#include "esp32m/dev/ina3221.hpp"

namespace esp32m {

  namespace ina3221 {

    static const uint16_t MaskConfig = 0x7C00;

    void Settings::setMode(bool continuous, bool bus, bool shunt) {
      if (_config.esht != shunt) {
        _config.esht = shunt;
        _modified = true;
      }
      if (_config.ebus != bus) {
        _config.ebus = bus;
        _modified = true;
      }
      if (_config.mode != continuous) {
        _config.mode = continuous;
        _modified = true;
      }
    }
    void Settings::enableChannels(bool c1, bool c2, bool c3) {
      if (_config.ch1 != c1) {
        _config.ch1 = c1;
        _modified = true;
      }
      if (_config.ch2 != c2) {
        _config.ch2 = c2;
        _modified = true;
      }
      if (_config.ch3 != c3) {
        _config.ch3 = c3;
        _modified = true;
      }
    }
    void Settings::enableSumChannels(bool c1, bool c2, bool c3) {
      if (_mask.scc1 != c1) {
        _mask.scc1 = c1;
        _modified = true;
      }
      if (_mask.scc2 != c2) {
        _mask.scc2 = c2;
        _modified = true;
      }
      if (_mask.scc3 != c3) {
        _mask.scc3 = c3;
        _modified = true;
      }
    }
    void Settings::enableLatch(bool c1, bool warning, bool critical) {
      if (_mask.wen != warning) {
        _mask.wen = warning;
        _modified = true;
      }
      if (_mask.cen != critical) {
        _mask.cen = critical;
        _modified = true;
      }
    }
    void Settings::setAverage(Average avg) {
      if (_config.avg != avg) {
        _config.avg = avg;
        _modified = true;
      }
    }
    void Settings::setBusConversionTime(ConversionTime ct) {
      if (_config.vbus != ct) {
        _config.vbus = ct;
        _modified = true;
      }
    }
    void Settings::setShuntConversionTime(ConversionTime ct) {
      if (_config.vsht != ct) {
        _config.vsht = ct;
        _modified = true;
      }
    }
    void Settings::setShunts(uint16_t s1, uint16_t s2, uint16_t s3) {
      _shunts[0] = s1;
      _shunts[1] = s2;
      _shunts[2] = s3;
    }
    bool ne(float a, float b) {
      if (isnan(a) && isnan(b))
        return true;
      if (isnan(a) || isnan(b))
        return false;
      return fabs(a - b) < std::numeric_limits<float>::epsilon();
    }
    void Settings::setCriticalAlert(Channel ch, float critical) {
      if (ne(_criticalAlerts[ch], critical)) {
        _criticalAlerts[ch] = critical;
        _modified = true;
      }
    }
    void Settings::setWarningAlert(Channel ch, float warning) {
      if (ne(_warningAlerts[ch], warning)) {
        _warningAlerts[ch] = warning;
        _modified = true;
      }
    }
    void Settings::setSumWarning(float sumWarning) {
      if (ne(_sumWarningAlert, sumWarning)) {
        _sumWarningAlert = sumWarning;
        _modified = true;
      }
    }
    void Settings::setPowerValidRange(float upper, float lower) {
      if (ne(_powerValidUpper, upper)) {
        _powerValidUpper = upper;
        _modified = true;
      }
      if (ne(_powerValidLower, lower)) {
        _powerValidLower = lower;
        _modified = true;
      }
    }

    bool Settings::isEnabled(Channel ch) const {
      switch (ch) {
        case Channel::First:
          return _config.ch1;
        case Channel::Second:
          return _config.ch2;
        case Channel::Third:
          return _config.ch3;
        case Channel::Sum:
          return _mask.scc1 || _mask.scc2 || _mask.scc3;
        default:
          return false;
      }
    }

    Core::Core(i2c::MasterDev* i2c) : _i2c(i2c) {}

    esp_err_t Core::reset() {
      _settings._config.value = DefaultConfig;
      _settings._mask.value = DefaultMask;
      auto config = _settings._config;
      config.rst = 1;
      return _i2c->write(Register::ConfigValue, config.value);
    }

    esp_err_t Core::sync(bool force) {
      if (!force && !_settings._modified)
        return ESP_OK;
      uint16_t value = 0;
      ESP_CHECK_RETURN(_i2c->read(Register::ConfigValue, value));
      if (value != _settings._config.value)
        ESP_CHECK_RETURN(
            _i2c->write(Register::ConfigValue, _settings._config.value));
      ESP_CHECK_RETURN(_i2c->read(Register::MaskValue, value));
      if ((value & MaskConfig) != (_settings._mask.value & MaskConfig))
        ESP_CHECK_RETURN(
            _i2c->write(Register::MaskValue, _settings._mask.value));

      for (ina3221::Channel ch = ina3221::Channel::First;
           ch <= ina3221::Channel::Max; ch++) {
        float v = _settings._criticalAlerts[ch];
        if (!isnan(v))
          ESP_CHECK_RETURN(
              _i2c->write(Register::CriticalLimit1 + ch * 2,
                          (int16_t)(v * _settings._shunts[ch] * 0.2)));
        v = _settings._warningAlerts[ch];
        if (!isnan(v))
          ESP_CHECK_RETURN(
              _i2c->write(Register::WarningLimit1 + ch * 2,
                          (int16_t)(v * _settings._shunts[ch] * 0.2)));
      }

      if (!isnan(_settings._sumWarningAlert))
        ESP_CHECK_RETURN(
            _i2c->write(Register::ShuntVoltageSumLimit,
                        (int16_t)(_settings._sumWarningAlert * 50.0)));
      if (!isnan(_settings._powerValidUpper) && _settings._config.ebus)
        ESP_CHECK_RETURN(
            _i2c->write(Register::ValidPowerUpper,
                        (int16_t)(_settings._powerValidUpper * 1000.0)));
      if (!isnan(_settings._powerValidLower) && _settings._config.ebus)
        ESP_CHECK_RETURN(
            _i2c->write(Register::ValidPowerLower,
                        (int16_t)(_settings._powerValidLower * 1000.0)));

      _settings._modified = false;
      return ESP_OK;
    }

    esp_err_t Core::readShunt(Channel ch, float* voltage, float* current) {
      if (!voltage && !current)
        return ESP_ERR_INVALID_ARG;
      ESP_CHECK_RETURN(sync());
      int16_t value = 0;
      Register reg = ch == Channel::Sum
                         ? Register::ShuntVoltageSum
                         : (Register)(Register::ShuntVoltage1 + ch * 2);
      ESP_CHECK_RETURN(_i2c->read(reg, value));
      float mvolts;
      uint16_t shunt = 0;
      if (ch == Channel::Sum) {
        mvolts = value * 0.02;
        if (_settings._mask.scc1)
          shunt = _settings._shunts[0];
        if (_settings._mask.scc2) {
          if (!shunt)
            shunt = _settings._shunts[1];
          else if (shunt != _settings._shunts[1])
            return ESP_ERR_INVALID_ARG;
        }
        if (_settings._mask.scc3) {
          if (!shunt)
            shunt = _settings._shunts[2];
          else if (shunt != _settings._shunts[2])
            return ESP_ERR_INVALID_ARG;
        }
      } else {
        mvolts = value * 0.005;  // mV, 40uV step
        shunt = _settings._shunts[ch];
      }
      if (voltage)
        *voltage = mvolts;
      if (current && shunt)
        *current = mvolts * 1000.0 / shunt;  // mA
      return ESP_OK;
    }

    esp_err_t Core::readBus(Channel ch, float& voltage) {
      ESP_CHECK_RETURN(sync());
      int16_t value = 0;
      ESP_CHECK_RETURN(
          _i2c->read((Register)(Register::BusVoltage1 + ch * 2), value));
      voltage = value * 0.001;
      return ESP_OK;
    }
  }  // namespace ina3221

  namespace dev {

    Ina3221::Ina3221(i2c::MasterDev* i2c) : ina3221::Core(i2c) {
      auto group = sensor::nextGroup();
      char idbuf[16];

      for (ina3221::Channel ch = ina3221::Channel::First;
           ch < ina3221::Channel::Max; ch++) {
        snprintf(idbuf, sizeof(idbuf), "shunt-%d", ch + 1);
        auto sv = _shuntVoltages[ch] = new Sensor(this, "voltage", idbuf);
        sv->unit = "mV";
        sv->precision = 2;
        sv->group = group;
        snprintf(idbuf, sizeof(idbuf), "bus-%d", ch + 1);
        auto bv = _busVoltages[ch] = new Sensor(this, "voltage", idbuf);
        bv->unit = "V";
        bv->precision = 2;
        bv->group = group;
        snprintf(idbuf, sizeof(idbuf), "current-%d", ch + 1);
        auto c = _currents[ch] = new Sensor(this, "current", idbuf);
        c->unit = "mA";
        c->precision = 2;
        c->group = group;
      }
      Device::init(Flags::HasSensors);
    }

    bool Ina3221::initSensors() {
      return sync(true) == ESP_OK;
    }

    bool Ina3221::pollSensors() {
      bool changed = false;
      for (ina3221::Channel ch = ina3221::Channel::First;
           ch < ina3221::Channel::Max; ch++)
        if (settings().isEnabled(ch)) {
          float value;
          if (readBus(ch, value) != ESP_OK)
            return false;
          _busVoltages[ch]->set(value, &changed);
          float sv, si;
          if (readShunt(ch, &sv, &si) != ESP_OK)
            return false;
          _shuntVoltages[ch]->set(sv, &changed);
          _currents[ch]->set(si, &changed);
        }
      if (changed)
        sensor::GroupChanged::publish(
            _busVoltages[ina3221::Channel::First]->group);
      return true;
    }

    JsonDocument* Ina3221::getState(RequestContext& ctx) {
      auto doc = new JsonDocument(
          /*JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(ina3221::Channel::Max + 1) +
          (ina3221::Channel::Max + 1) * JSON_ARRAY_SIZE(3)*/);
      JsonObject state = doc->to<JsonObject>();
      state["addr"] = _i2c->address();
      auto c = state["channels"].to<JsonArray>();
      for (ina3221::Channel ch = ina3221::Channel::First;
           ch < ina3221::Channel::Max; ch++)
        if (settings().isEnabled(ch)) {
          auto a = c.add<JsonArray>();
          float value;
          if (readBus(ch, value) != ESP_OK)
            value = NAN;
          a.add(value);
          float sv, si;
          if (readShunt(ch, &sv, &si) != ESP_OK) {
            sv = NAN;
            si = NAN;
          }
          a.add(sv);
          a.add(si);
        } else
          c.add(nullptr);
      return doc;
    }
    void Ina3221::setState(RequestContext& ctx) {}
    bool Ina3221::setConfig(RequestContext& ctx) {
      return false;
    }
    JsonDocument* Ina3221::getConfig(RequestContext& ctx) {
      return nullptr;
    }

    void useIna3221(uint8_t address) {
      new Ina3221(i2c::MasterDev::create(address));
    }
  }  // namespace dev
}  // namespace esp32m