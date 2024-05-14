#pragma once

#include "esp32m/bus/i2c.hpp"
#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {

  namespace cap11xx {

    enum class Register {
      MainControl = 0x00,
      InputStatus = 0x03,
      MultiTouch = 0x2a,
      StandbyConfig = 0x41,
      LedLinking = 0x72,
      LedPolarity = 0x73,

      ProductId = 0xfd,
      ManufacturerId = 0xfe,
      Revision = 0xff,
    };

    enum class StandbyCycleTime {
      Ms35 = 0,
      Ms70 = 1,
      Ms105 = 2,
      Ms140 = 3,
      Default = Ms70,
    };

    enum class StandbySampeTime {
      Us320 = 0,
      Us640 = 1,
      Us1280 = 2,
      Us2560 = 3,
      Default = Us1280,
    };

    enum class StandbyAvgSamples {
      S1 = 0,
      S2 = 1,
      S4 = 2,
      S8 = 3,
      S16 = 4,
      S32 = 5,
      S64 = 6,
      S128 = 7,
      Default = S8,
    };

    const uint8_t DefaultAddress = 0x29;
    const uint8_t ProdIdCap1126 = 0x53;
    const uint8_t ProdIdCap1188 = 0x50;
    const uint8_t ManufacturerId = 0x5D;

    class Core : public virtual log::Loggable {
     public:
      Core();
      Core(const Core &) = delete;
      const char *name() const override {
        return _name;
      }
      esp_err_t reset();
      esp_err_t resetInt();
      esp_err_t read(Register reg, uint8_t &value);
      esp_err_t write(Register reg, uint8_t value);
      esp_err_t setMultitouch(bool enable = false);
      esp_err_t setLedLinking(bool led1 = false, bool led2 = false);
      esp_err_t setLedPolarityInversion(bool led1 = true, bool led2 = true);
      esp_err_t setStandby(
          StandbyCycleTime cycleTime = StandbyCycleTime::Default,
          StandbySampeTime sampleTime = StandbySampeTime::Default,
          StandbyAvgSamples avgSamples = StandbyAvgSamples::Default,
          bool useSummation = false);
      esp_err_t getTouchBitmap(uint8_t &bitmap);
      bool isTouched(int num);

     protected:
      std::unique_ptr<I2C> _i2c;
      io::pin::IDigital *_reset = nullptr;
      const char *_name;
      esp_err_t init(I2C *i2c, io::IPin *resetPin = nullptr,
                     const char *name = nullptr);

     private:
      uint8_t _prodId = 0;
    };
  }  // namespace cap11xx

  namespace dev {
    class Cap11xx : public virtual Device, public virtual cap11xx::Core {
     public:
      Cap11xx(I2C *i2c, io::IPin *resetPin = nullptr,
              const char *name = nullptr);
      Cap11xx(const Cap11xx &) = delete;

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;

     private:
      uint64_t _stamp = 0;
    };

    Cap11xx *useCap11xx(uint8_t addr = cap11xx::DefaultAddress,
                        io::IPin *resetPin = nullptr,
                        const char *name = nullptr);
  }  // namespace dev
}  // namespace esp32m