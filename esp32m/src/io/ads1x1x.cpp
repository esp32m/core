#include "esp32m/defs.hpp"

#include "esp32m/io/ads1x1x.hpp"

namespace esp32m {
  namespace io {
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
    }  // namespace ads1x1x

    Ads1x1x::Ads1x1x(I2C *i2c, ads1x1x::Variant v) : _i2c(i2c), _variant(v) {
      _i2c->setEndianness(Endian::Big);
      _gain = ads1x1x::Gain::TwoThirds;
      if (_variant == ads1x1x::Variant::Ads101x) {
        _rate = ads1x1x::Rate::ADS1015_1600SPS;
        _bitShift = 4;
      } else {
        _rate = ads1x1x::Rate::ADS1115_128SPS;
        _bitShift = 0;
      }
      IPins::init(8);
    }

    esp_err_t Ads1x1x::isBusy(bool &value) {
      uint16_t config;
      ESP_CHECK_RETURN(_i2c->read(ads1x1x::Register::Config, config));
      value = (config & ads1x1x::ConfigMaskOs) == 0;
      return ESP_OK;
    }

    esp_err_t Ads1x1x::readValue(int16_t &value) {
      ESP_CHECK_RETURN(_i2c->read(ads1x1x::Register::Conversion, value));
      if (_variant == ads1x1x::Variant::Ads101x) {
        value = value >> 4;
        if (value > 0x07FF)
          // negative number - extend the sign to 16th bit
          value |= 0xF000;
      }
      return ESP_OK;
    }
    esp_err_t Ads1x1x::startReading(ads1x1x::Mux mux, bool continuous) {
      // Start with default values
      uint16_t config =
          ads1x1x::ConfigCque1Conv |    // Set CQUE to any value other than
                                        // None so we can use it in RDY mode
          ads1x1x::ConfigClatNonlat |   // Non-latching (default val)
          ads1x1x::ConfigCpolActvLow |  // Alert/Rdy active low   (default val)
          ads1x1x::ConfigCmodeTrad;     // Traditional comparator (default val)

      if (continuous)
        config |= ads1x1x::ConfigModeCont;
      else
        config |= ads1x1x::ConfigModeSingle;

      // Set PGA/voltage range
      config |= _gain;

      // Set data rate
      config |= _rate;

      // Set channels
      config |= mux;

      // Set 'start single-conversion' bit
      config |= ads1x1x::ConfigModeOsSingle;

      // Write config register to the ADC
      ESP_CHECK_RETURN(_i2c->write(ads1x1x::Register::Config, config));

      // Set ALERT/RDY to RDY mode.
      ESP_CHECK_RETURN(
          _i2c->write(ads1x1x::Register::ThreshH, (uint16_t)0x8000));
      ESP_CHECK_RETURN(
          _i2c->write(ads1x1x::Register::ThreshL, (uint16_t)0x8000));
      return ESP_OK;
    }
    esp_err_t Ads1x1x::readMux(ads1x1x::Mux mux, int16_t &value,
                               bool continuous) {
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
    esp_err_t Ads1x1x::computeVolts(int16_t counts, float &volts) {
      // see data sheet Table 3
      float fsRange;
      switch (_gain) {
        case ads1x1x::Gain::TwoThirds:
          fsRange = 6.144f;
          break;
        case ads1x1x::Gain::One:
          fsRange = 4.096f;
          break;
        case ads1x1x::Gain::Two:
          fsRange = 2.048f;
          break;
        case ads1x1x::Gain::Four:
          fsRange = 1.024f;
          break;
        case ads1x1x::Gain::Eight:
          fsRange = 0.512f;
          break;
        case ads1x1x::Gain::Sixteen:
          fsRange = 0.256f;
          break;
        default:
          fsRange = 0.0f;
      }
      volts = counts * (fsRange / (32768 >> _bitShift));
      return ESP_OK;
    }

    namespace ads1x1x {
      class Pin : public IPin {
       public:
        Pin(Ads1x1x *owner, int id) : IPin(id), _owner(owner) {
          const char *n;
          switch (id) {
            case 0:
              n = "d01";
              break;
            case 1:
              n = "d03";
              break;
            case 2:
              n = "d13";
              break;
            case 3:
              n = "d23";
              break;
            case 4:
              n = "s0";
              break;
            case 5:
              n = "s1";
              break;
            case 6:
              n = "s2";
              break;
            case 7:
              n = "s3";
              break;
            default:
              n = "?";
              break;
          }
          snprintf(_name, sizeof(_name), "%s-%s", owner->name(), n);
        };
        const char *name() const override {
          return _name;
        }
        io::pin::Flags flags() override {
          return io::pin::Flags::ADC;
        }
        esp_err_t createFeature(pin::Type type,
                                pin::Feature **feature) override;

       private:
        Ads1x1x *_owner;
        char _name[12];
        friend class ADC;
      };

      class ADC : public pin::IADC {
       public:
        ADC(Pin *pin) : _pin(pin) {}
        esp_err_t read(int &value, uint32_t *mv) override {
          Mux mux = (Mux)(_pin->id() << 12);
          int16_t v;
          ESP_CHECK_RETURN(_pin->_owner->readMux(mux, v));
          value = v;
          if (mv) {
            float volts;
            ESP_CHECK_RETURN(_pin->_owner->computeVolts(value, volts));
            *mv = (uint32_t)(volts * 1000);
          }
          return ESP_OK;
        }
        esp_err_t range(int &min, int &max, uint32_t *mvMin = nullptr,
                        uint32_t *mvMax = nullptr) override {
          min = 0;
          max = 32768 >> _pin->_owner->_bitShift;
          if (mvMin)
            *mvMin = 0;
          if (mvMax)
            *mvMax = 3300;
          return ESP_OK;
        }
        adc_atten_t getAtten() override {
          return _atten;
        }
        esp_err_t setAtten(adc_atten_t atten) override {
          _atten = atten;
          return ESP_OK;
        }
        int getWidth() override {
          return _width;
        }
        esp_err_t setWidth(adc_bitwidth_t width) override {
          _width = width;
          return ESP_OK;
        }

       private:
        Pin *_pin;
        adc_bitwidth_t _width = ADC_BITWIDTH_12;
        adc_atten_t _atten = AdcAttenDefault;
      };

      esp_err_t Pin::createFeature(pin::Type type, pin::Feature **feature) {
        switch (type) {
          case pin::Type::ADC:
            *feature = new ADC(this);
            return ESP_OK;
          default:
            break;
        }
        return IPin::createFeature(type, feature);
      }

    }  // namespace ads1x1x

    IPin *Ads1x1x::newPin(int id) {
      return new ads1x1x::Pin(this, id);
    }

  }  // namespace io
}  // namespace esp32m
