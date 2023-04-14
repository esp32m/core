#pragma once

#include "esp32m/bus/i2c.hpp"
#include "esp32m/device.hpp"

namespace esp32m {

  namespace ads1x1x {

    const uint8_t DefaultAddress = 0x48;

    enum Variant {
      Ads111x,
      Ads101x,
    };

    enum Mux {
      Diff01 = 0x0000,   ///< Differential P = AIN0, N = AIN1 (default)
      Diff03 = 0x1000,   ///< Differential P = AIN0, N = AIN3
      Diff13 = 0x2000,   ///< Differential P = AIN1, N = AIN3
      Diff23 = 0x3000,   ///< Differential P = AIN2, N = AIN3
      Single0 = 0x4000,  ///< Single-ended AIN0
      Single1 = 0x5000,  ///< Single-ended AIN1
      Single2 = 0x6000,  ///< Single-ended AIN2
      Single3 = 0x7000   ///< Single-ended AIN3
    };

    enum Gain {
      TwoThirds = 0x0000,
      One = 0x0200,
      Two = 0x0400,
      Four = 0x0600,
      Eight = 0x0800,
      Sixteen = 0x0A00
    };

    enum Rate {
      ADS1015_128SPS = 0x0000,   ///< 128 samples per second
      ADS1015_250SPS = 0x0020,   ///< 250 samples per second
      ADS1015_490SPS = 0x0040,   ///< 490 samples per second
      ADS1015_920SPS = 0x0060,   ///< 920 samples per second
      ADS1015_1600SPS = 0x0080,  ///< 1600 samples per second (default)
      ADS1015_2400SPS = 0x00A0,  ///< 2400 samples per second
      ADS1015_3300SPS = 0x00C0,  ///< 3300 samples per second

      ADS1115_8SPS = 0x0000,    ///< 8 samples per second
      ADS1115_16SPS = 0x0020,   ///< 16 samples per second
      ADS1115_32SPS = 0x0040,   ///< 32 samples per second
      ADS1115_64SPS = 0x0060,   ///< 64 samples per second
      ADS1115_128SPS = 0x0080,  ///< 128 samples per second (default)
      ADS1115_250SPS = 0x00A0,  ///< 250 samples per second
      ADS1115_475SPS = 0x00C0,  ///< 475 samples per second
      ADS1115_860SPS = 0x00E0,  ///< 860 samples per second
    };

    class Core : public virtual log::Loggable {
     public:
      Core(I2C *i2c, Variant v);
      Core(const Core &) = delete;
      const char *name() const override {
        switch (_variant) {
          case Variant::Ads101x:
            return "ADS101x";
          case Variant::Ads111x:
            return "ADS111x";
          default:
            return "ADS1x1x";
        }
      }

      esp_err_t isBusy(bool &value);
      esp_err_t readValue(int16_t &value);
      esp_err_t startReading(Mux mux, bool continuous = false);
      esp_err_t readMux(Mux mux, int16_t &value, bool continuous = false);
      esp_err_t computeVolts(int16_t counts, float &volts);
      void setGain(Gain gain) {
        _gain = gain;
      }
      Gain getGain() {
        return _gain;
      }
      void setRate(Rate rate) {
        _rate = rate;
      }
      Rate getRate() {
        return _rate;
      }

     protected:
      std::unique_ptr<I2C> _i2c;

     private:
      Variant _variant;
      Gain _gain;
      Rate _rate;
      uint8_t _bitShift;
    };

  }  // namespace ads1x1x
}  // namespace esp32m
