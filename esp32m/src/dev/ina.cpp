#include "esp32m/dev/ina.hpp"

#include <math.h>

namespace esp32m {
  namespace ina {

    enum ConfigBit {
      Rst = 15,  // self-reset bit, common for all INA sensors
    };

    const char *type2name(Type type) {
      switch (type) {
        case Type::Ina219:
          return "INA219";
        case Type::Ina226:
          return "INA226";
        case Type::Ina230:
          return "INA230";
        case Type::Ina231:
          return "INA231";
        case Type::Ina260:
          return "INA260";
        case Type::Ina3221_0:
          return "INA3221-0";
        case Type::Ina3221_1:
          return "INA3221-1";
        case Type::Ina3221_2:
          return "INA3221-2";
        default:
          return "unknown";
      }
    }

    Core::Core(I2C *i2c, const char *name, Type type)
        : _i2c(i2c), _name(name), _type(type) {}

    const char *Core::name() const {
      if (_name)
        return _name;
      return type2name(_type);
    }
    bool Core::is3221() const {
      switch (_type) {
        case Type::Ina3221_0:
        case Type::Ina3221_1:
        case Type::Ina3221_2:
          return true;
        default:
          return false;
      }
    }
    esp_err_t Core::sync(bool force) {
      std::lock_guard guard(_i2c->mutex());
      if (_type == Type::Unknown)
        ESP_CHECK_RETURN(detect());
      if (_type == Type::Unknown)
        return ESP_ERR_NOT_FOUND;
      if ((_flags & Flags::ConfigLoaded) == 0) {
        ESP_CHECK_RETURN(_i2c->read(Register::Conf, _config.value));
        logD("initial config: 0x%x", _config.value);
        _flags |= Flags::ConfigLoaded;
      }
      bool updateConfig = false;
      if (_config.mode != _mode) {
        _config.mode = _mode;
        updateConfig = true;
      }
      if ((_flags & Flags::AvgChanged) != 0) {
        if (_type == Type::Ina219) {
          int i = 7;
          while (i > 0)
            if (_avgSamples >= (1 << i))
              break;
            else
              i--;
          i |= 8;
          if (_config.ina219.sadc != i || _config.ina219.badc != i) {
            _config.ina219.sadc = i;
            _config.ina219.badc = i;
            updateConfig = true;
          }
        } else {
          int i = 7;
          while (i > 0)
            if (_avgSamples >= (1 << (i < 4 ? (i << 1) : i + 3)))
              break;
            else
              i--;
          if (_config.ina226_23x.avg != i) {
            _config.ina226_23x.avg = i;
            updateConfig = true;
          }
        }
        _flags &= ~Flags::AvgChanged;
      }

      if ((_flags & Flags::Inited) == 0)
        ESP_CHECK_RETURN(init());
      if ((_flags & Flags::Calibrated) == 0)
        ESP_CHECK_RETURN(calibrate());

      if (force || updateConfig)
        changeConfig();
      return ESP_OK;
    }

    esp_err_t Core::changeConfig() {
      Config config = {};
      ESP_CHECK_RETURN(_i2c->read(Register::Conf, config.value));
      if (_config.value != config.value) {
        ESP_CHECK_LOGW_RETURN(_i2c->write(Register::Conf, _config.value),
                              "failed to write config");
        uint16_t nc = 0;
        ESP_CHECK_RETURN(_i2c->read(Register::Conf, nc));
        if (nc != _config.value) {
          logW("failed to change config: 0x%x != 0x%x", nc, _config.value);
          _config.value = nc;
        } else
          logD("config changed to 0x%x", _config.value);
      }
      return ESP_OK;
    }

    esp_err_t Core::init() {
      if (is3221()) {
        _lsbBus = 800;
        _lsbShunt = 400;
      } else {
        _lsbShunt = 0;
        _lsbBus = 0;
      }
      _regBus = Register::Bus;
      _regShunt = -1;
      _regCurrent = -1;
      switch (_type) {
        case Type::Ina219:
          _regShunt = 1;
          _regCurrent = 4;
          _lsbBus = 400;
          _lsbShunt = 100;
          break;
        case Type::Ina226:
        case Type::Ina230:
        case Type::Ina231:
          _regShunt = 1;
          _regCurrent = 4;
          _lsbBus = 125;
          _lsbShunt = 25;
          break;
        case Type::Ina260:
          _lsbCurrent = 1250000;  // Fixed LSB of 1.25mv
          _lsbPower = 10000000;   // Fixed multiplier per device
          _regCurrent = 1;
          _lsbBus = 125;
          break;
        case Type::Ina3221_0:
          _regShunt = 1;
          break;
        case Type::Ina3221_1:
          _regBus += 2;
          _regShunt += 2;
          break;
        case Type::Ina3221_2:
          _regBus += 4;
          _regShunt += 4;
          break;
        default:
          return ESP_FAIL;
      }

      _flags |= Flags::Inited;
      return ESP_OK;
    }

    esp_err_t Core::calibrate() {
      int8_t gain;  // work variable for the programmable gain
      uint32_t calibration = 0, maxShuntmV;
      bool updateConfig = false, calibrate = false;
      if (is3221()) {
        _lsbCurrent = 0;
        _lsbPower = 0;
      } else {
        _lsbCurrent = ((uint64_t)_maxCurrent_mA * 1000000) >> 15;
        _lsbPower = (uint32_t)20 * _lsbCurrent;
      }
      switch (_type) {
        case Type::Ina219:
          calibration =
              (uint64_t)40960000000 / ((uint64_t)_lsbCurrent * _shunt_mOhm);
          calibrate = true;
          maxShuntmV = _maxCurrent_mA * _shunt_mOhm / 1000;
          if (maxShuntmV <= 40)
            gain = 0;  // gain x1 for +- 40mV
          else if (maxShuntmV <= 80)
            gain = 1;  // gain x2 for +- 80mV
          else if (maxShuntmV <= 160)
            gain = 2;  // gain x4 for +- 160mV
          else
            gain = 3;
          if (_config.ina219.pg != gain) {
            _config.ina219.pg = gain;
            updateConfig = true;
          }
          if (!_config.ina219.brng) {
            _config.ina219.brng = 1;
            updateConfig = true;
          }
          break;
        case Type::Ina226:
        case Type::Ina230:
        case Type::Ina231:
          calibration =
              (uint64_t)5120000000 / ((uint64_t)_lsbCurrent * _shunt_mOhm);
          calibrate = true;
          _lsbPower = (uint32_t)25 * _lsbCurrent;
          break;
        default:
          break;
      }
      if (updateConfig)
        changeConfig();
      if (calibrate) {
        if (calibration > 32767) {
          logW("calibration value is too high: %u", calibration);
          return ESP_FAIL;
        }
        ESP_CHECK_RETURN(
            _i2c->write(Register::Calibration, (uint16_t)calibration));
        logD("calibration: %u lsb: %u shunt: %u", calibration, _lsbCurrent,
             _shunt_mOhm);
      }
      _flags |= Flags::Calibrated;
      return ESP_OK;
    }

    Mode Core::getMode() const {
      return (Mode)_config.mode;
    }
    void Core::setMode(Mode value) {
      _mode = value;
    }
    void Core::setShuntMilliOhms(uint16_t value) {
      if (value == _shunt_mOhm)
        return;
      _shunt_mOhm = value;
      _flags &= ~Flags::Calibrated;
    }
    uint16_t Core::getShuntMilliOhms() const {
      return _shunt_mOhm;
    }
    void Core::setMaxCurrentMilliAmps(uint32_t value) {
      if (value == _maxCurrent_mA)
        return;
      _maxCurrent_mA = value;
      _flags &= ~Flags::Calibrated;
    }
    uint32_t Core::getMaxCurrentMilliAmps() const {
      return _maxCurrent_mA;
    }

    uint16_t Core::getAveraging() const {
      if (_type == Type::Ina219) {
        auto i = _config.ina219.sadc;
        if (i != _config.ina219.badc || (i & 8) != 8)
          return 0;
        return (1 << (i & 7));
      } else {
        int s = _config.ina226_23x.avg;
        return (1 << (s < 4 ? (s << 1) : s + 3));
      }
    }
    void Core::setAveraging(uint16_t samples) {
      if (_avgSamples == samples)
        return;
      _avgSamples = samples;
      _flags |= Flags::AvgChanged;
    }

    esp_err_t Core::reset() {
      std::lock_guard guard(_i2c->mutex());
      Config c = {};
      c.rst = 1;
      ESP_CHECK_RETURN(_i2c->write(Register::Conf, c.value));
      return _i2c->read(Register::Conf, _config.value);
    }

    esp_err_t Core::detect() {
      const uint16_t rst = 1 << ConfigBit::Rst;
      uint16_t reg0 = 0, config = 0, die = 0;
      _type = Type::Unknown;
      ESP_CHECK_RETURN(_i2c->read(Register::Conf, reg0));
      ESP_CHECK_RETURN(_i2c->write(Register::Conf, rst));
      ESP_CHECK_RETURN(_i2c->read(Register::Conf, config));
      switch (config) {
        case rst:
          ESP_CHECK_RETURN(_i2c->write(Register::Conf, reg0));
          break;
        case 0x399F:
          _type = Type::Ina219;
          break;
        case 0x4127:  // INA226, INA230, INA231
          ESP_CHECK_RETURN(_i2c->read(Register::DieId, die));
          switch (die) {
            case 0x2260:
              _type = Type::Ina226;
              break;
            case 0x0:
              _type = Type::Ina231;
              break;
            default:
              _type = Type::Ina230;
              break;
          }
          break;
        case 0x6127:
          _type = Type::Ina260;
          break;
        case 0x7127:
          _type = Type::Ina3221_0;
          break;
        default:
          break;
      }
      return _type == Type::Unknown ? ESP_ERR_NOT_FOUND : ESP_OK;
    }

    esp_err_t Core::trigger() {
      if (_config.modeBits.cont) {
        logW("could not trigger measurement in continuous mode");
        return ESP_ERR_INVALID_STATE;
      }
      return _i2c->writeSafe(Register::Conf, _config.value);
    }

    esp_err_t Core::getBusRaw(uint16_t &value) {
      std::lock_guard guard(_i2c->mutex());
      uint16_t raw = 0;
      ESP_CHECK_RETURN(_i2c->read(_regBus, raw));
      if (is3221() || _type == Type::Ina219)
        raw = raw >> 3;  // INA219 & INA3221 - the 3 LSB unused, so shift right
      value = raw;
      return ESP_OK;
    }
    esp_err_t Core::getShuntRaw(int16_t &value) {
      if (_type == Type::Ina260)  // INA260 has a built-in shunt
      {
        float amps;
        ESP_CHECK_RETURN(getAmps(amps));
        value =
            (int16_t)(amps / 0.2);  // 2mOhm resistor, convert with Ohm's law
      } else {
        int16_t raw = 0;
        std::lock_guard guard(_i2c->mutex());
        ESP_CHECK_RETURN(_i2c->read(_regShunt, raw));
        // logI("reg-shunt %i = %i, lsb: %u", _regShunt, raw, _lsbShunt);
        if (is3221())
          raw = raw >> 3;
        value = raw;
      }
      return ESP_OK;
    }

    esp_err_t Core::getBusVolts(float &value) {
      uint16_t raw;
      value = NAN;
      ESP_CHECK_RETURN(getBusRaw(raw));
      value = (float)raw * _lsbBus / 100000;
      return ESP_OK;
    }
    esp_err_t Core::getShuntVolts(float &value) {
      value = NAN;
      if (_type == Type::Ina260)  // INA260 has a built-in shunt
      {
        float amps;
        ESP_CHECK_RETURN(getAmps(amps));
        value = amps / 0.002;  // 2mOhm resistor, convert with Ohm's law
      } else {
        int16_t raw;
        ESP_CHECK_RETURN(getShuntRaw(raw));
        value =
            (float)raw * _lsbShunt / 10 / 1000 / 1000;  // _lsbShunt is uV*10
      }
      return ESP_OK;
    }
    esp_err_t Core::getAmps(float &value) {
      value = NAN;
      if (is3221()) {
        float sv;
        ESP_CHECK_RETURN(getShuntVolts(sv));
        value = sv * _shunt_mOhm / 1000;
      } else {
        int16_t raw = 0;
        std::lock_guard guard(_i2c->mutex());
        ESP_CHECK_RETURN(_i2c->read(_regCurrent, raw));
        // logD("amps %i = %i, lsb: %u", _regCurrent, raw, _lsbCurrent);
        value = (float)raw * _lsbCurrent / 1000000 / 1000;
      }
      return ESP_OK;
    }
    esp_err_t Core::getWatts(float &value) {
      value = NAN;
      if (is3221()) {
        float sv, bv;
        ESP_CHECK_RETURN(getShuntVolts(sv));
        ESP_CHECK_RETURN(getBusVolts(bv));
        value = bv * sv * _shunt_mOhm / 1000;
      } else {
        uint16_t raw = 0;
        std::lock_guard guard(_i2c->mutex());
        ESP_CHECK_RETURN(_i2c->read(Register::Power, raw));
        value = (float)raw * _lsbPower / 1000000 / 1000;
      }
      return ESP_OK;
    }

  }  // namespace ina

  namespace dev {

    Ina::Ina(I2C *i2c)
        : ina::Core(i2c),
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

    DynamicJsonDocument *Ina::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(7));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_i2c->addr());
      arr.add(ina::type2name(type()));
      float value;
      getBusVolts(value);
      arr.add(value);
      getShuntVolts(value);
      arr.add(value);
      getAmps(value);
      arr.add(value);
      getWatts(value);
      arr.add(value);
      return doc;
    }

    bool Ina::pollSensors() {
      ESP_CHECK_RETURN_BOOL(sync());
      bool changed = false;
      float value;
      ESP_CHECK_RETURN_BOOL(getBusVolts(value));
      sensor("voltage", value);
      _voltage.set(value, &changed);
      ESP_CHECK_RETURN_BOOL(getShuntVolts(value));
      sensor("shuntVoltage", value);
      ESP_CHECK_RETURN_BOOL(getAmps(value));
      sensor("current", value);
      _current.set(value, &changed);
      ESP_CHECK_RETURN_BOOL(getWatts(value));
      sensor("power", value);
      _power.set(value, &changed);
      _stamp = millis();
      if (changed)
        sensor::GroupChanged::publish(_voltage.group);

      return true;
    }
    bool Ina::initSensors() {
      return sync(true) == ESP_OK;
    }

    Ina *useIna(uint8_t addr) {
      return new Ina(new I2C(addr));
    }

  }  // namespace dev
}  // namespace esp32m