#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

#include "esp32m/dev/cap11xx.hpp"

namespace esp32m {

  namespace cap11xx {
    Core::Core() {}

    esp_err_t Core::init(I2C *i2c, io::IPin *resetPin, const char *name) {
      _i2c.reset(i2c);
      _name = name ? name : "CAP11xx";
      _reset = resetPin ? resetPin->digital() : nullptr;
      if (_reset) {
        ESP_CHECK_RETURN(_reset->setDirection(false, true));
        ESP_CHECK_RETURN(_reset->setPull(false, true));
        ESP_CHECK_RETURN(_reset->write(false));
      }
      ESP_CHECK_RETURN(reset());
      uint8_t mid;
      ESP_CHECK_RETURN(read(Register::ManufacturerId, mid));
      if (mid != ManufacturerId) {
        logW("unsupported manufacturer ID 0x%x (expected 0x%x)", mid,
             ManufacturerId);
        return ESP_ERR_NOT_SUPPORTED;
      }
      ESP_CHECK_RETURN(read(Register::ProductId, _prodId));
      uint8_t revision;
      ESP_CHECK_RETURN(read(Register::Revision, revision));
      switch (_prodId) {
        case ProdIdCap1126:
          logI("CAP1126 found, revision 0x%x", revision);
          break;
        case ProdIdCap1188:
          logI("CAP1188 found, revision 0x%x", revision);
          break;
        default:
          logW("unsupported product ID 0x%x", _prodId);
          return ESP_ERR_NOT_SUPPORTED;
      }
      return ESP_OK;
    }

    esp_err_t Core::reset() {
      if (_reset) {
        ESP_CHECK_RETURN(_reset->write(false));
        delay(100);
        ESP_CHECK_RETURN(_reset->write(true));
        delay(100);
        ESP_CHECK_RETURN(_reset->write(false));
        delay(100);
      }
      return ESP_OK;
    }

    esp_err_t Core::setMultitouch(bool enable) {
      return write(Register::MultiTouch, enable ? 0x00 : 0x80);
    }
    esp_err_t Core::setLedLinking(bool led1, bool led2) {
      return write(Register::LedLinking, (led1 ? 1 : 0) | (led2 ? 2 : 0));
    }
    esp_err_t Core::setLedPolarityInversion(bool led1, bool led2) {
      return write(Register::LedPolarity, (led1 ? 0 : 1) | (led2 ? 0 : 1));
    }

    esp_err_t Core::setStandby(StandbyCycleTime cycleTime,
                               StandbySampeTime sampleTime,
                               StandbyAvgSamples avgSamples,
                               bool useSummation) {
      return write(Register::StandbyConfig, ((uint8_t)cycleTime) |
                                                ((uint8_t)sampleTime << 2) |
                                                ((uint8_t)avgSamples << 4) |
                                                ((useSummation ? 1 : 0) << 7));
    }
    esp_err_t Core::getTouchBitmap(uint8_t &bitmap) {
      return read(Register::InputStatus, bitmap);
    }
    bool Core::isTouched(int num) {
      uint8_t bits;
      ESP_CHECK_RETURN_BOOL(getTouchBitmap(bits));
      return bits & (1 << num);
    }
    esp_err_t Core::read(Register reg, uint8_t &value) {
      return _i2c->readSafe((uint8_t)reg, value);
    }
    esp_err_t Core::write(Register reg, uint8_t value) {
      return _i2c->writeSafe((uint8_t)reg, value);
    }

  }  // namespace cap11xx
  namespace dev {

    Cap11xx::Cap11xx(I2C *i2c, io::IPin *resetPin, const char *name) {
      Device::init(Flags::None);
      cap11xx::Core::init(i2c, resetPin, name);
    }

    DynamicJsonDocument *Cap11xx::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(3));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      /*      float t, h;
            if (getReadings(&t, &h) == ESP_OK) {
              arr.add(t);
              arr.add(h);
            }*/
      return doc;
    }

    Cap11xx *useCap11xx(uint8_t addr, io::IPin *resetPin, const char *name) {
      return new Cap11xx(new I2C(addr), resetPin, name);
    }

  }  // namespace dev

}  // namespace esp32m
