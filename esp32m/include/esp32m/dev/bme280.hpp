#pragma once

#include "esp32m/bus/i2c.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  class Bme280;

  namespace bme280 {

    const uint8_t DefaultAddress = 0x76;

    enum Mode { Sleep = 0, Forced = 1, Normal = 3 };

    enum Oversampling {
      Skipped,
      UltraLowPower,
      LowPower,
      Standard,
      HighRes,
      UltraHighRes
    };

    enum Filter { Off, x2, x4, x8, x16 };

    enum Standby { Ms05, Ms62, Ms125, Ms250, Ms500, S1, S2, S4 };

    struct Calibration {
      uint16_t T1;
      int16_t T2;
      int16_t T3;
      uint16_t P1;
      int16_t P2;
      int16_t P3;
      int16_t P4;
      int16_t P5;
      int16_t P6;
      int16_t P7;
      int16_t P8;
      int16_t P9;
      uint8_t H1;
      int16_t H2;
      uint8_t H3;
      int16_t H4;
      int16_t H5;
      int8_t H6;
    };

    enum Register {
      CalT1 = 0x88,
      CalT2 = 0x8A,
      CalT3 = 0x8C,
      CalP1 = 0x8E,
      CalP2 = 0x90,
      CalP3 = 0x92,
      CalP4 = 0x94,
      CalP5 = 0x96,
      CalP6 = 0x98,
      CalP7 = 0x9A,
      CalP8 = 0x9C,
      CalP9 = 0x9E,
      CalH1 = 0xA1,
      Id = 0xD0,
      Reset = 0xE0,
      CalH2 = 0xE1,
      CalH3 = 0xE3,
      CalH4 = 0xE4,
      CalH5 = 0xE5,
      CalH6 = 0xE7,
      CtrlHum = 0xF2,
      Status = 0xF3,
      Ctrl = 0xF4,
      Config = 0xF5,
      PressureMsb = 0xF7,
      PressureLsb = 0xF8,
      PressureXLsb = 0xF9,
      TemperatureMsb = 0xFA,
      TemperatureLsb = 0xFB,
      TemperatureXLsb = 0xFC,
    };

    enum ChipId { Unknown = 0x00, Bmp280 = 0x58, Bme280 = 0x60 };

    struct Settings {
     public:
      Mode getMode() const;
      void setMode(Mode value);
      Filter getFilter() const;
      void setFilter(Filter value);
      Standby getStandby() const;
      void setStandby(Standby value);
      void getOversampling(Oversampling *pressure, Oversampling *temperature,
                           Oversampling *humidity) const;
      void setOversampling(Oversampling pressure, Oversampling temperature,
                           Oversampling humidity);

     private:
      Mode _mode = Mode::Normal;
      Filter _filter = Filter::Off;
      Standby _standby = Standby::Ms250;
      Oversampling _ovsPressure = Oversampling::Standard,
                   _ovsTemperature = Oversampling::Standard,
                   _ovsHumidity = Oversampling::Standard;
      bool _modified = true;
      friend class Core;
    };

    class Core : public virtual log::Loggable {
     public:
      Core(I2C *i2c, const char *name);
      Core(const Core &) = delete;
      const char *name() const override {
        return _name;
      }
      ChipId chipId() const {
        return _id;
      }
      Settings &settings() {
        return _settings;
      }
      esp_err_t sync(bool force = false);
      esp_err_t measure();
      esp_err_t isMeasuring(bool &busy);
      esp_err_t readFixed(int32_t *temperature, uint32_t *pressure,
                          uint32_t *humidity);
      esp_err_t read(float *temperature, float *pressure, float *humidity);

     protected:
      std::unique_ptr<I2C> _i2c;

     private:
      const char *_name;
      Calibration _cal = {};
      ChipId _id = ChipId::Unknown;
      bool _inited = false;
      Settings _settings;
      esp_err_t syncUnsafe(bool force = false);
      esp_err_t readCalibration();
      esp_err_t writeConfig();
      int32_t compTemperature(int32_t adcT, int32_t &fineT);
      uint32_t compPressure(int32_t adcP, int32_t fineT);
      uint32_t compHumidity(int32_t adcH, int32_t fineT);
      friend class esp32m::Bme280;
    };
  }  // namespace bme280

  namespace dev {
    class Bme280 : public virtual Device, public virtual bme280::Core {
     public:
      Bme280(I2C *i2c, const char *name);
      Bme280(const Bme280 &) = delete;

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool handleRequest(Request &req) override;
      bool initSensors() override;
      bool pollSensors() override;

     private:
      Sensor _temperature, _pressure, _humidity;
    };

    void useBme280(const char *name = nullptr,
                   uint8_t addr = bme280::DefaultAddress);
  }  // namespace dev
}  // namespace esp32m