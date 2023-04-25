#include "esp32m/defs.hpp"

#include "esp32m/dev/ads1x1x.hpp"

namespace esp32m {

  namespace ads1x1x {

    enum Register { Conversion = 0, Config = 1, ThreshL = 2, ThreshH = 3 };

    static const int ConfigShiftOs = 15;
    static const int ConfigMaskOs = 1 << ConfigShiftOs;

    static const int ConfigCque1Conv = 0x0000;
    static const int ConfigCque2Conv = 0x0001;
    static const int ConfigCque4Conv = 0x0002;
    static const int ConfigCqueNone = 0x0003;

    static const int ConfigClatNonlat = 0x0000;
    static const int ConfigClatLatch = 0x0004;

    static const int ConfigCpolActvLow = 0x0000;
    static const int ConfigCpolActvHi = 0x0008;

    static const int ConfigCmodeTrad = 0x0000;
    static const int ConfigCmodeWindow = 0x0010;

    static const int ConfigModeCont = 0x0000;
    static const int ConfigModeSingle = 0x0100;

    static const int ConfigModeOsSingle = 0x8000;

    Core::Core(I2C *i2c, Variant v) : _i2c(i2c), _variant(v) {
      _i2c->setEndianness(Endian::Big);
      _gain = Gain::TwoThirds;
      if (_variant == Variant::Ads101x) {
        _rate = Rate::ADS1015_1600SPS;
        _bitShift = 4;
      } else {
        _rate = Rate::ADS1115_128SPS;
        _bitShift = 0;
      }
    }

    esp_err_t Core::isBusy(bool &value) {
      uint16_t config;
      ESP_CHECK_RETURN(_i2c->read(Register::Config, config));
      value = (config & ConfigMaskOs) == 0;
      return ESP_OK;
    }

    esp_err_t Core::readValue(int16_t &value) {
      ESP_CHECK_RETURN(_i2c->read(Register::Conversion, value));
      if (_variant == Variant::Ads101x) {
        value = value >> 4;
        if (value > 0x07FF)
          // negative number - extend the sign to 16th bit
          value |= 0xF000;
      }
      return ESP_OK;
    }
    esp_err_t Core::startReading(Mux mux, bool continuous) {
      // Start with default values
      uint16_t config =
          ConfigCque1Conv |    // Set CQUE to any value other than
                               // None so we can use it in RDY mode
          ConfigClatNonlat |   // Non-latching (default val)
          ConfigCpolActvLow |  // Alert/Rdy active low   (default val)
          ConfigCmodeTrad;     // Traditional comparator (default val)

      if (continuous)
        config |= ConfigModeCont;
      else
        config |= ConfigModeSingle;

      // Set PGA/voltage range
      config |= _gain;

      // Set data rate
      config |= _rate;

      // Set channels
      config |= mux;

      // Set 'start single-conversion' bit
      config |= ConfigModeOsSingle;

      // Write config register to the ADC
      ESP_CHECK_RETURN(_i2c->write(Register::Config, config));

      // Set ALERT/RDY to RDY mode.
      ESP_CHECK_RETURN(_i2c->write(Register::ThreshH, (uint16_t)0x8000));
      ESP_CHECK_RETURN(_i2c->write(Register::ThreshL, (uint16_t)0x8000));
      return ESP_OK;
    }
    esp_err_t Core::readMux(Mux mux, int16_t &value, bool continuous) {
      ESP_CHECK_RETURN(startReading(mux, continuous));
      bool busy = true;
      for (;;) {
        ESP_CHECK_RETURN(isBusy(busy));
        if (!busy)
          break;
        delay(1);
      }
      return readValue(value);
    }
    esp_err_t Core::computeVolts(int16_t counts, float &volts) {
      // see data sheet Table 3
      float fsRange;
      switch (_gain) {
        case Gain::TwoThirds:
          fsRange = 6.144f;
          break;
        case Gain::One:
          fsRange = 4.096f;
          break;
        case Gain::Two:
          fsRange = 2.048f;
          break;
        case Gain::Four:
          fsRange = 1.024f;
          break;
        case Gain::Eight:
          fsRange = 0.512f;
          break;
        case Gain::Sixteen:
          fsRange = 0.256f;
          break;
        default:
          fsRange = 0.0f;
      }
      volts = counts * (fsRange / (32768 >> _bitShift));
      return ESP_OK;
    }

  }  // namespace ads1x1x
}  // namespace esp32m
