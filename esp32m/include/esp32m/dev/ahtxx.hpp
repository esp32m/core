#pragma once

#include "esp32m/bus/i2c/master.hpp"
#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {

  namespace ahtxx {

    const uint8_t DefaultAddress = 0x38;

    enum class Command {
      Status = 0x71,
      Init1x = 0xE1,
      Init2x = 0xBE,
      Measure = 0xAC,
      Reset = 0xBA,
    };

    const uint8_t StatusFifoFull = 1 >> 1;
    const uint8_t StatusFifoOn = 1 >> 2;
    const uint8_t StatusCalibrated = 1 >> 3;
    const uint8_t StatusCRC = 1 >> 4;
    const uint8_t StatusBusy = 1 >> 7;

    class Core : public virtual log::Loggable {
     public:
      Core(const char *name) {
        _name = name ? name : "AHTXX";
      }
      Core(const Core &) = delete;
      const char *name() const override {
        return _name;
      }
      esp_err_t start() {
        delay(500);
        uint8_t status;
        ESP_CHECK_RETURN(getStatus(status));
        logI("status: 0x%02x", status);
        if ((status & (StatusCalibrated | StatusCRC)) !=
            (StatusCalibrated | StatusCRC)) {
          resetReg(0x1b);
          resetReg(0x1c);
          resetReg(0x1e);
          delay(10);
        }
        return ESP_OK;
      }

     protected:
      std::unique_ptr<i2c::MasterDev> _i2c;
      const char *_name;

      esp_err_t init(i2c::MasterDev *i2c) {
        _i2c.reset(i2c);
        delay(100);
        return cmd(Command::Init2x, 0x8);
      }

      esp_err_t cmd(Command cmd, uint16_t arg) {
        uint8_t tx[3] = {(uint8_t)cmd, (uint8_t)(arg & 0xff),
                         (uint8_t)((arg >> 8) & 0xff)};
        return _i2c->write(nullptr, 0, tx, sizeof(tx));
      }
      esp_err_t getStatus(uint8_t &status) {
        uint8_t cmd = (uint8_t)Command::Status;
        return _i2c->read(&cmd, 1, &status, sizeof(status));
      }

      esp_err_t measure(float &t, float &h) {
        ESP_CHECK_RETURN(startMeasure());
        delay(80);
        ESP_CHECK_RETURN(waitBusy());
        uint8_t data[7] = {};
        ESP_CHECK_RETURN(_i2c->read(nullptr, 0, data, sizeof(data)));
        if (crc(data, 6) != data[6])
          return ESP_ERR_INVALID_CRC;
        h = ((data[1] << 12) | (data[2] << 4) | ((data[3] & 0xf0) >> 4)) *
            100.0 / 1048576;
        t = (((data[3] & 0xf) << 16) | (data[4] << 8) | (data[5])) * 200.0 /
                1048576 -
            50.0;
        return ESP_OK;
      }

     private:
      esp_err_t resetReg(uint8_t reg) {
        uint8_t cmd[3] = {reg, 0x0, 0x00};
        ESP_CHECK_RETURN(_i2c->read(cmd, sizeof(cmd), cmd, sizeof(cmd)));
        logD("reset reg 0x%02x: 0x%02x 0x%02x 0x%02x", reg, cmd[0], cmd[1],
             cmd[2]);
        cmd[0] = 0xB0 | reg;
        ESP_CHECK_RETURN(_i2c->write(cmd, sizeof(cmd), nullptr, 0));
        return ESP_OK;
      }
      esp_err_t startMeasure() {
        return cmd(Command::Measure, 0x33);
      }
      esp_err_t waitBusy() {
        uint8_t status;
        int cnt = 0;
        do {
          delay(2);
          ESP_CHECK_RETURN(getStatus(status));
          if (cnt++ > 100)
            return ESP_FAIL;
        } while (status & StatusBusy);
        return ESP_OK;
      }
      static uint8_t crc(uint8_t *data, uint8_t len) {
        uint8_t i;
        uint8_t byte;
        uint8_t crc = 0xFF;

        for (byte = 0; byte < len; byte++) {
          crc ^= data[byte];
          for (i = 8; i > 0; --i) {
            if ((crc & 0x80) != 0)
              crc = (crc << 1) ^ 0x31;
            else
              crc = crc << 1;
          }
        }

        return crc;
      }
    };

  }  // namespace ahtxx

  namespace dev {
    class Ahtxx : public virtual Device, public virtual ahtxx::Core {
     public:
      Ahtxx(i2c::MasterDev *i2c, const char *name = nullptr)
          : ahtxx::Core(name),
            _temperature(this, "temperature"),
            _humidity(this, "humidity") {
        auto group = sensor::nextGroup();
        _temperature.group = group;
        _temperature.precision = 2;
        _humidity.group = group;
        _humidity.precision = 0;
        Device::init(Flags::HasSensors);
        ahtxx::Core::init(i2c);
      }
      Ahtxx(const Ahtxx &) = delete;

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override {
        DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(3));
        JsonArray arr = doc->to<JsonArray>();
        arr.add(millis() - _stamp);
        arr.add(_temperature.get());
        arr.add(_humidity.get());
        return doc;
      }
      bool initSensors() override {
        return start() == ESP_OK;
      }
      bool pollSensors() override {
        float t, h;
        ESP_CHECK_RETURN_BOOL(measure(t, h));
        bool changed = false;
        _temperature.set(t, &changed);
        _humidity.set(h, &changed);
        if (changed)
          sensor::GroupChanged::publish(_temperature.group);
        _stamp = millis();
        return true;
      }

     private:
      Sensor _temperature, _humidity;
      uint64_t _stamp = 0;
    };

    Ahtxx *useAhtxx(uint8_t addr = ahtxx::DefaultAddress,
                    const char *name = nullptr);
  }  // namespace dev
}  // namespace esp32m