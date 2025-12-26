#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

#include "esp32m/dev/sht3x.hpp"

namespace esp32m {

  namespace sht3x {

    const uint16_t CmdStatus = 0xF32D;
    const uint16_t CmdClearStatus = 0x3041;
    const uint16_t CmdArt = 0x2b32;
    const uint16_t CmdReset = 0x30A2;
    const uint16_t CmdFetchData = 0xE000;
    const uint16_t CmdStopPeriodic = 0x3093;
    const uint16_t CmdHeaterOn = 0x306D;
    const uint16_t CmdHeaterOff = 0x3066;

    static const uint16_t CmdMeasure[7][3] = {
        {0x2400, 0x240b, 0x2416}, {0x2C06, 0x2C0d, 0x2C10},
        {0x2032, 0x2024, 0x202f}, {0x2130, 0x2126, 0x212d},
        {0x2236, 0x2220, 0x222b}, {0x2334, 0x2322, 0x2329},
        {0x2737, 0x2721, 0x272a}};

    static const uint16_t DurationUs[3] = {15000, 6000, 4000};

    static uint8_t crc8(uint8_t data[], int len) {
      const uint8_t Polynom = 0x31;
      uint8_t crc = 0xff;
      for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int i = 0; i < 8; i++) {
          bool x = (crc & 0x80) != 0;
          crc = crc << 1;
          crc = x ? crc ^ Polynom : crc;
        }
      }
      return crc;
    }

    Core::Core() {}

    esp_err_t Core::reset(Mode mode, Repeatability repeatability) {
      if (!_i2c)
        return ESP_ERR_INVALID_STATE;
      _mode = mode;
      _repeatability = repeatability;
      _start = 0;
      _first = false;
      _dataValid = false;
      auto err = cmd(CmdReset);
      delay(10);
      return err;
    }

    esp_err_t Core::init(i2c::MasterDev* i2c, io::IPin* resetPin, const char* name) {
      _i2c.reset(i2c);
      _name = name ? name : "SHT3X";
      _i2c->setEndianness(Endian::Little);
      _reset = resetPin ? resetPin->digital() : nullptr;
      if (_reset) {
        _reset->setDirection(true, true);
        _reset->setPull(true, false);
        _reset->write(true);
      }
      return reset(Mode::Single, Repeatability::Medium);
    }

    esp_err_t Core::cmd(uint16_t c) {
      c = toEndian(Endian::Big, c);
      return _i2c->write(nullptr, 0, &c, sizeof(c));
    }

    esp_err_t Core::setHeater(bool on) {
      if (!_i2c)
        return ESP_ERR_INVALID_STATE;
      return cmd(on ? CmdHeaterOn : CmdHeaterOff);
    }

    esp_err_t Core::start() {
      if (!_i2c)
        return ESP_ERR_INVALID_STATE;
      if (_mode == Mode::Single || _mode == Mode::SingleStretched || !_start) {
        ESP_CHECK_RETURN(cmd(CmdMeasure[_mode][_repeatability]));
        _start = micros();
        _first = true;
        _dataValid = false;
      }
      return ESP_OK;
    }

    void Core::waitMeasured() {
      auto start = _start;
      if (start && _first) {
        auto d = DurationUs[_repeatability];
        uint64_t elapsed = micros() - _start;
        if (elapsed < d)
          delayUs(d - elapsed);
      }
    }

    bool Core::isMeasuring() {
      if (!_start || !_first)
        return false;
      uint64_t elapsed = micros() - _start;
      return elapsed < DurationUs[_repeatability];
    }

    esp_err_t Core::read() {
      _dataValid = false;
      if (!_start || isMeasuring())
        return ESP_ERR_INVALID_STATE;
      uint16_t c = toEndian(Endian::Big, CmdFetchData);
      ESP_CHECK_RETURN(_i2c->read(&c, sizeof(c), _data, sizeof(_data)));
      _first = false;
      if (_mode == Mode::Single || _mode == Mode::SingleStretched)
        _start = 0;
      if (crc8(_data, 2) != _data[2]) {
        logW("invalid temperature CRC");
        return ESP_ERR_INVALID_CRC;
      }
      if (crc8(_data + 3, 2) != _data[5]) {
        logW("invalid humidity CRC");
        return ESP_ERR_INVALID_CRC;
      }
      _dataValid = true;
      return ESP_OK;
    }

    esp_err_t Core::getReadings(float* temperature, float* humidity) {
      if (!_dataValid)
        return ESP_ERR_INVALID_STATE;
      if (temperature)
        *temperature = ((((_data[0] << 8) | _data[1]) * 175) / 65535.0) - 45;
      if (humidity)
        *humidity = ((((_data[3] << 8) | _data[4]) * 100) / 65535.0);
      return ESP_OK;
    }

    esp_err_t Core::measure(float* temperature, float* humidity) {
      ESP_CHECK_RETURN(start());
      waitMeasured();
      ESP_CHECK_RETURN(read());
      ESP_CHECK_RETURN(getReadings(temperature, humidity));
      return ESP_OK;
    }

  }  // namespace sht3x

  namespace dev {

    Sht3x::Sht3x(i2c::MasterDev* i2c, io::IPin* resetPin, const char* name)
        : _temperature(this, "temperature"), _humidity(this, "humidity") {
      auto group = sensor::nextGroup();
      _temperature.group = group;
      _temperature.precision = 2;
      _humidity.group = group;
      _humidity.precision = 0;
      Device::init(Flags::HasSensors);
      sht3x::Core::init(i2c, resetPin, name);
    }

    bool Sht3x::initSensors() {
      /*ESP_CHECK_RETURN_BOOL(
          reset(sht3x::Mode::Periodic1Mps, sht3x::Repeatability::Medium));*/
      return true;
    }

    bool Sht3x::pollSensors() {
      float t, h;
      ESP_CHECK_RETURN_BOOL(measure(&t, &h));
      bool changed = false;
      _temperature.set(t, &changed);
      _humidity.set(h, &changed);
      if (changed)
        sensor::GroupChanged::publish(_temperature.group);
      _stamp = millis();
      return true;
    }

    JsonDocument* Sht3x::getState(RequestContext& ctx) {
      JsonDocument* doc = new JsonDocument();
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      float t, h;
      if (getReadings(&t, &h) == ESP_OK) {
        arr.add(t);
        arr.add(h);
      }
      return doc;
    }

    Sht3x* useSht3x(uint8_t addr, io::IPin* resetPin, const char* name) {
      return new Sht3x(i2c::MasterDev::create(addr), resetPin, name);
    }

  }  // namespace dev

}  // namespace esp32m