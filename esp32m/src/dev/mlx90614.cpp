#include "esp32m/dev/mlx90614.hpp"

#include <math.h>

namespace esp32m {
  namespace mlx90614 {

    uint8_t crc8(uint8_t *addr, int len)
    // The PEC calculation includes all bits except the START, REPEATED START,
    // STOP, ACK, and NACK bits. The PEC is a CRC-8 with polynomial X8+X2+X1+1.
    {
      uint8_t crc = 0;
      while (len--) {
        uint8_t inbyte = *addr++;
        for (uint8_t i = 8; i; i--) {
          uint8_t carry = (crc ^ inbyte) & 0x80;
          crc <<= 1;
          if (carry)
            crc ^= 0x7;
          inbyte <<= 1;
        }
      }
      return crc;
    }

    Core::Core(I2C *i2c, const char *name) : _i2c(i2c), _name(name) {}

    esp_err_t Core::init() {
      uint16_t id[4];
      ESP_CHECK_RETURN(read(Register::ID1, id[0]));
      ESP_CHECK_RETURN(read(Register::ID2, id[1]));
      ESP_CHECK_RETURN(read(Register::ID3, id[2]));
      ESP_CHECK_RETURN(read(Register::ID4, id[3]));
      logI("ID: %x %x %x %x", id[0], id[1], id[2], id[3]);
      return ESP_OK;
    }

    esp_err_t Core::getEmissivity(float &value) {
      value = NAN;
      uint16_t raw;
      ESP_CHECK_RETURN(read(Register::Emissivity, raw));
      if (raw != 0)
        value = (float)raw / 65535;
      return ESP_OK;
    }
    esp_err_t Core::getObjectTemperature(float &value) {
      return readTemp(Register::TObj1, value);
    }
    esp_err_t Core::getAmbientTemperature(float &value) {
      return readTemp(Register::Ta, value);
    }

    esp_err_t Core::read(Register reg, uint16_t &value) {
      std::lock_guard guard(_i2c->mutex());
      uint8_t data[3];
      ESP_CHECK_RETURN(_i2c->read(reg, data, sizeof(data)));
      uint8_t addr = (uint8_t)(_i2c->addr() << 1);
      uint8_t crcbuf[5]{addr, reg, (uint8_t)(addr | 1), data[0], data[1]};
      auto pec = crc8(crcbuf, sizeof(crcbuf));
      if (pec != data[2]) {
        logW("crc mismatch: %x != %x", pec, data[2]);
        // return ESP_ERR_INVALID_CRC;
      }
      value = fromEndian(Endian::Little, *(uint16_t *)data);
      return ESP_OK;
    }

    esp_err_t Core::write(Register reg, uint16_t value) {
      uint8_t buffer[4];

      buffer[0] = _i2c->addr() << 1;
      buffer[1] = reg;
      ((uint16_t *)buffer)[1] = toEndian(Endian::Little, value);

      uint8_t pec = crc8(buffer, 4);

      buffer[0] = buffer[2];
      buffer[1] = buffer[3];
      buffer[2] = pec;

      std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(_i2c->write(reg, buffer, 3));
      return ESP_OK;
    }

    esp_err_t Core::readTemp(Register reg, float &temp) {
      temp = NAN;
      uint16_t raw;
      ESP_CHECK_RETURN(read(reg, raw));
      if (raw != 0)
        temp = (float)raw * 0.02 - 273.15;
      return ESP_OK;
    }

  }  // namespace mlx90614
  namespace dev {
    Mlx90614::Mlx90614(I2C *i2c)
        : Device(Flags::HasSensors), mlx90614::Core(i2c) {}

    DynamicJsonDocument *Mlx90614::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(6));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_i2c->addr());
      float value;
      getAmbientTemperature(value);
      arr.add(value);
      getObjectTemperature(value);
      arr.add(value);
      return doc;
    }

    bool Mlx90614::pollSensors() {
      float value;
      ESP_CHECK_RETURN_BOOL(getAmbientTemperature(value));
      sensor("ta", value);
      ESP_CHECK_RETURN_BOOL(getObjectTemperature(value));
      sensor("tobj1", value);
      _stamp = millis();
      return true;
    }
    bool Mlx90614::initSensors() {
      return init() == ESP_OK;
    }

    Mlx90614 *useMlx90614(uint8_t addr) {
      return new Mlx90614(new I2C(addr));
    }

  }  // namespace dev
}  // namespace esp32m
