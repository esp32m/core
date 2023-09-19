#include "esp32m/defs.hpp"

#include "esp32m/dev/bme280.hpp"
// #include "esp32m/ha/ha.hpp"

namespace esp32m {

  namespace bme280 {

    Mode Settings::getMode() const {
      return _mode;
    }
    void Settings::setMode(Mode value) {
      if (_mode == value)
        return;
      _mode = value;
      _modified = true;
    }
    Filter Settings::getFilter() const {
      return _filter;
    }
    void Settings::setFilter(Filter value) {
      if (_filter == value)
        return;
      _filter = value;
      _modified = true;
    }
    Standby Settings::getStandby() const {
      return _standby;
    }
    void Settings::setStandby(Standby value) {
      if (_standby == value)
        return;
      _standby = value;
      _modified = true;
    }
    void Settings::getOversampling(Oversampling *pressure,
                                   Oversampling *temperature,
                                   Oversampling *humidity) const {
      if (pressure)
        *pressure = _ovsPressure;
      if (temperature)
        *temperature = _ovsTemperature;
      if (humidity)
        *humidity = _ovsHumidity;
    }
    void Settings::setOversampling(Oversampling pressure,
                                   Oversampling temperature,
                                   Oversampling humidity) {
      _ovsPressure = pressure;
      _ovsTemperature = temperature;
      _ovsHumidity = humidity;
    }

    Core::Core(I2C *i2c, const char *name) : _i2c(i2c) {
      _name = name;
      _i2c->setEndianness(Endian::Little);
      _i2c->setErrSnooze(30000);
      uint8_t id = 0;
      if (_i2c->readSafe(Register::Id, id) == ESP_OK)
        _id = (ChipId)id;
      if (!_name)
        switch (_id) {
          case ChipId::Bmp280:
            _name = "BMP280";
            break;
          case ChipId::Bme280:
            _name = "BME280";
            break;
          default:
            _name = "BMx280";
            break;
        }
    }

    esp_err_t Core::readCalibration() {
      ESP_CHECK_RETURN(_i2c->read(Register::CalT1, _cal.T1));
      ESP_CHECK_RETURN(_i2c->read(Register::CalT2, _cal.T2));
      ESP_CHECK_RETURN(_i2c->read(Register::CalT3, _cal.T3));
      ESP_CHECK_RETURN(_i2c->read(Register::CalP1, _cal.P1));
      ESP_CHECK_RETURN(_i2c->read(Register::CalP2, _cal.P2));
      ESP_CHECK_RETURN(_i2c->read(Register::CalP3, _cal.P3));
      ESP_CHECK_RETURN(_i2c->read(Register::CalP4, _cal.P4));
      ESP_CHECK_RETURN(_i2c->read(Register::CalP5, _cal.P5));
      ESP_CHECK_RETURN(_i2c->read(Register::CalP6, _cal.P6));
      ESP_CHECK_RETURN(_i2c->read(Register::CalP7, _cal.P7));
      ESP_CHECK_RETURN(_i2c->read(Register::CalP8, _cal.P8));
      if (_id == ChipId::Bme280) {
        ESP_CHECK_RETURN(_i2c->read(Register::CalH1, _cal.H1));
        ESP_CHECK_RETURN(_i2c->read(Register::CalH2, _cal.H2));
        ESP_CHECK_RETURN(_i2c->read(Register::CalH3, _cal.H3));
        uint16_t h4, h5;
        ESP_CHECK_RETURN(_i2c->read(Register::CalH4, h4));
        ESP_CHECK_RETURN(_i2c->read(Register::CalH5, h5));
        ESP_CHECK_RETURN(_i2c->read(Register::CalH6, _cal.H6));
        _cal.H4 = (h4 & 0x00ff) << 4 | (h4 & 0x0f00) >> 8;
        _cal.H5 = h5 >> 4;
      }
      return ESP_OK;
    }

    esp_err_t Core::writeConfig() {
      uint8_t config = (_settings._standby << 5) | (_settings._filter << 2);
      ESP_CHECK_LOGW_RETURN(_i2c->write(Register::Config, config),
                            "failed to configure");
      if (_id == ChipId::Bme280) {
        // Write crtl hum reg first, only active after write to BMP280_REG_CTRL.
        uint8_t ctrl_hum = _settings._ovsHumidity;
        ESP_CHECK_LOGW_RETURN(_i2c->write(Register::CtrlHum, ctrl_hum),
                              "failed to configure humidity oversampling");
      }
      uint8_t mode =
          _settings._mode == Mode::Forced ? Mode::Sleep : _settings._mode;
      uint8_t ctrl = (_settings._ovsTemperature << 5) |
                     (_settings._ovsPressure << 2) | mode;
      ESP_CHECK_LOGW_RETURN(_i2c->write(Register::Ctrl, ctrl),
                            "failed to configure mode & oversampling");
      return ESP_OK;
    }

    esp_err_t Core::sync(bool force) {
      std::lock_guard guard(_i2c->mutex());
      return syncUnsafe(force);
    }

    esp_err_t Core::syncUnsafe(bool force) {
      static const int ResetValue = 0xB6;
      if (!force && !_settings._modified && _id != ChipId::Unknown)
        return ESP_OK;
      uint8_t id = 0;
      ESP_CHECK_RETURN(_i2c->read(Register::Id, id));
      bool idChanged = id != _id;
      _id = (ChipId)id;
      if (id != ChipId::Bme280 && id != ChipId::Bmp280) {
        if (idChanged)
          logW("chip id 0x%02x is not supported by the BMx280 driver", id);
        return ESP_ERR_NOT_SUPPORTED;
      }
      if (idChanged)
        _inited = false;
      if (!_inited) {
        ESP_CHECK_LOGW_RETURN(_i2c->write(Register::Reset, ResetValue),
                              "failed to reset");
        auto t = micros();
        for (;;) {
          uint8_t status = 0;
          if (!_i2c->read(Register::Status, status) && (status & 1) == 0)
            break;
          if (micros() - t > 1000 * 1000) {
            logW("reset timeout");
            return ESP_ERR_TIMEOUT;
          }
        }
        ESP_CHECK_LOGW_RETURN(readCalibration(),
                              "failed to read calabration data");
        _inited = true;
      }
      ESP_CHECK_RETURN(writeConfig());
      _settings._modified = false;
      return ESP_OK;
    }

    esp_err_t Core::measure() {
      std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(syncUnsafe());
      uint8_t ctrl = 0;
      ESP_CHECK_RETURN(_i2c->read(Register::Ctrl, ctrl));
      ctrl &= ~0b11;  // clear two lower bits
      ctrl |= Mode::Forced;
      ESP_CHECK_LOGW_RETURN(_i2c->write(Register::Ctrl, ctrl),
                            "failed to force measurement");
      return ESP_OK;
    }

    esp_err_t Core::isMeasuring(bool &busy) {
      static const uint8_t regs[2] = {Register::Status, Register::Ctrl};
      std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(syncUnsafe());
      uint8_t status[2];
      ESP_CHECK_LOGW_RETURN(_i2c->read(regs, 2, &status, 2),
                            "failed to read status");
      // Check mode - FORCED means BM280 is busy (it switches to SLEEP mode when
      // finished) Additionally, check 'measuring' bit in status register
      busy = ((status[1] & 0b11) == Mode::Forced) || (status[0] & (1 << 3));
      return ESP_OK;
    }

    int32_t Core::compTemperature(int32_t adcT, int32_t &fineT) {
      int32_t var1, var2;

      var1 =
          ((((adcT >> 3) - ((int32_t)_cal.T1 << 1))) * (int32_t)_cal.T2) >> 11;
      var2 = (((((adcT >> 4) - (int32_t)_cal.T1) *
                ((adcT >> 4) - (int32_t)_cal.T1)) >>
               12) *
              (int32_t)_cal.T3) >>
             14;

      fineT = var1 + var2;
      return (fineT * 5 + 128) >> 8;
    }

    uint32_t Core::compPressure(int32_t adcP, int32_t fineT) {
      int64_t var1, var2, p;

      var1 = (int64_t)fineT - 128000;
      var2 = var1 * var1 * (int64_t)_cal.P6;
      var2 = var2 + ((var1 * (int64_t)_cal.P5) << 17);
      var2 = var2 + (((int64_t)_cal.P4) << 35);
      var1 = ((var1 * var1 * (int64_t)_cal.P3) >> 8) +
             ((var1 * (int64_t)_cal.P2) << 12);
      var1 = (((int64_t)1 << 47) + var1) * ((int64_t)_cal.P1) >> 33;

      if (var1 == 0) {
        return 0;  // avoid exception caused by division by zero
      }

      p = 1048576 - adcP;
      p = (((p << 31) - var2) * 3125) / var1;
      var1 = ((int64_t)_cal.P9 * (p >> 13) * (p >> 13)) >> 25;
      var2 = ((int64_t)_cal.P8 * p) >> 19;

      p = ((p + var1 + var2) >> 8) + ((int64_t)_cal.P7 << 4);
      return p;
    }

    uint32_t Core::compHumidity(int32_t adcH, int32_t fineT) {
      int32_t v_x1_u32r;

      v_x1_u32r = fineT - (int32_t)76800;
      v_x1_u32r =
          ((((adcH << 14) - ((int32_t)_cal.H4 << 20) -
             ((int32_t)_cal.H5 * v_x1_u32r)) +
            (int32_t)16384) >>
           15) *
          (((((((v_x1_u32r * (int32_t)_cal.H6) >> 10) *
               (((v_x1_u32r * (int32_t)_cal.H3) >> 11) + (int32_t)32768)) >>
              10) +
             (int32_t)2097152) *
                (int32_t)_cal.H2 +
            8192) >>
           14);
      v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                                (int32_t)_cal.H1) >>
                               4);
      v_x1_u32r = v_x1_u32r < 0 ? 0 : v_x1_u32r;
      v_x1_u32r = v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r;
      return v_x1_u32r >> 12;
    }

    esp_err_t Core::readFixed(int32_t *temperature, uint32_t *pressure,
                              uint32_t *humidity) {
      int32_t adc_pressure;
      int32_t adc_temp;
      uint8_t data[8];
      std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(syncUnsafe());
      bool hum = _id == ChipId::Bme280 && humidity;
      size_t size = hum ? 8 : 6;
      ESP_CHECK_LOGW_RETURN(_i2c->read(Register::PressureMsb, data, size),
                            "failed to read measurements");
      adc_pressure = data[0] << 12 | data[1] << 4 | data[2] >> 4;
      adc_temp = data[3] << 12 | data[4] << 4 | data[5] >> 4;
      int32_t fine_temp, fixedT;
      fixedT = compTemperature(adc_temp, fine_temp);
      if (temperature)
        *temperature = fixedT;
      if (pressure)
        *pressure = compPressure(adc_pressure, fine_temp);
      if (hum) {
        int32_t adc_humidity = data[6] << 8 | data[7];
        *humidity = compHumidity(adc_humidity, fine_temp);
      } else if (humidity)
        *humidity = 0;
      return ESP_OK;
    }

    esp_err_t Core::read(float *temperature, float *pressure, float *humidity) {
      int32_t fixed_temperature;
      uint32_t fixed_pressure;
      uint32_t fixed_humidity;
      ESP_CHECK_RETURN(readFixed(&fixed_temperature, &fixed_pressure,
                                 humidity ? &fixed_humidity : nullptr));
      *temperature = (float)fixed_temperature / 100;
      *pressure = (float)fixed_pressure / 256;
      if (humidity)
        *humidity = (float)fixed_humidity / 1024;
      return ESP_OK;
    }

  }  // namespace bme280

  namespace dev {

    Bme280::Bme280(I2C *i2c, const char *name)
        : bme280::Core(i2c, name),
          _temperature(this, "temperature"),
          _pressure(this, "atmospheric_pressure"),
          _humidity(this, "humidity") {
      auto group = sensor::nextGroup();
      _temperature.group = group;
      _temperature.precision = 2;
      _pressure.group = group;
      _pressure.precision = 0;
      _pressure.unit = "mmHg";
      _humidity.group = group;
      _humidity.precision = 0;
      _humidity.disabled = chipId() != bme280::ChipId::Bme280;
      Device::init(Flags::HasSensors);
    }

    bool Bme280::initSensors() {
      return sync(true) == ESP_OK;
    }

    bool Bme280::pollSensors() {
      float t, p, h;
      ESP_CHECK_RETURN_BOOL(read(&t, &p, &h));
/*      sensor("temperature", t);
      sensor("pressure", p / 133.322F);
      if (chipId() == bme280::ChipId::Bme280)
        sensor("humidity", h);*/

      bool changed = false;
      _temperature.set(t, &changed);
      _pressure.set(p / 133.322F, &changed);
      if (chipId() == bme280::ChipId::Bme280)
        _humidity.set(h, &changed);
      if (changed)
        sensor::GroupChanged::publish(_temperature.group);
      return true;
    }

    DynamicJsonDocument *Bme280::getState(const JsonVariantConst args) {
      float t, p, h;
      if (read(&t, &p, &h) != ESP_OK)
        return nullptr;
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(4));
      JsonObject root = doc->to<JsonObject>();
      root["addr"] = _i2c->addr();
      root["temperature"] = t;
      root["pressure"] = p;
      if (chipId() == bme280::ChipId::Bme280)
        root["humidity"] = h;
      return doc;
    }

    void useBme280(const char *name, uint8_t addr) {
      new Bme280(new I2C(addr), name);
    }

  }  // namespace dev

}  // namespace esp32m