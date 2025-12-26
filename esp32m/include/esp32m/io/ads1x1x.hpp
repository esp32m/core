#pragma once

#include "esp32m/bus/i2c/master.hpp"
#include "esp32m/io/pins.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {

  namespace io {

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

      class ADC;

    }  // namespace ads1x1x

    class Ads1x1x : public virtual IPins {
     public:
      Ads1x1x(i2c::MasterDev *i2c, ads1x1x::Variant v);
      Ads1x1x(const Ads1x1x &) = delete;
      const char *name() const override {
        switch (_variant) {
          case ads1x1x::Variant::Ads101x:
            return "ADS101x";
          case ads1x1x::Variant::Ads111x:
            return "ADS111x";
          default:
            return "ADS1x1x";
        }
      }

      esp_err_t isBusy(bool &value);
      esp_err_t readValue(int16_t &value);
      esp_err_t startReading(ads1x1x::Mux mux, bool continuous = false);
      esp_err_t readMux(ads1x1x::Mux mux, int16_t &value, bool continuous = false);
      esp_err_t computeVolts(int16_t counts, float &volts);
      void setGain(ads1x1x::Gain gain) {
        _gain = gain;
      }
      ads1x1x::Gain getGain() {
        return _gain;
      }
      void setRate(ads1x1x::Rate rate) {
        _rate = rate;
      }
      ads1x1x::Rate getRate() {
        return _rate;
      }

     protected:
      std::unique_ptr<i2c::MasterDev> _i2c;
      IPin *newPin(int id) override;

     private:
      ads1x1x::Variant _variant;
      ads1x1x::Gain _gain;
      ads1x1x::Rate _rate;
      uint8_t _bitShift;
      friend class ads1x1x::ADC;
    };
  }  // namespace io
}  // namespace esp32m
