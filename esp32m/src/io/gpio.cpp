#include "esp32m/io/gpio.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/io/pins.hpp"
#include "esp32m/io/utils.hpp"

#include <driver/adc.h>
#include <driver/gpio.h>
#include <driver/pcnt.h>
#include <esp_adc_cal.h>
#include <sdkconfig.h>
#include <string.h>

#define DEFAULT_VREF \
  1100  // Use adc2_vref_to_gpio() to obtain a better
        // estimate

namespace esp32m {
  namespace gpio {
    enum ADCFlags {
      None = 0,
      Valid = BIT0,
      CharsDirty = BIT0,
    };

    ENUM_FLAG_OPERATORS(ADCFlags)

    class ADC : public io::pin::IADC {
     public:
      ADC(gpio_num_t pin) : _pin(pin) {
        if (init() == ESP_OK)
          _flags |= ADCFlags::Valid;
      }
      bool valid() override {
        return (_flags & ADCFlags::Valid) != 0;
      }
      esp_err_t read(int &value, uint32_t *mv) override {
        if (_c1 >= 0) {
          value = adc1_get_raw((adc1_channel_t)_c1);
          if (value < 0)
            return ESP_FAIL;
        } else
          ESP_CHECK_RETURN(adc2_get_raw((adc2_channel_t)_c2, _width, &value));
        if (mv) {
          if (!_adcChars) {
            _adcChars = (esp_adc_cal_characteristics_t *)calloc(
                1, sizeof(esp_adc_cal_characteristics_t));
            _flags |= ADCFlags::CharsDirty;
          }
          if ((_flags & ADCFlags::CharsDirty) != 0) {
            _flags &= ~ADCFlags::CharsDirty;
            memset(_adcChars, 0, sizeof(esp_adc_cal_characteristics_t));
            esp_adc_cal_characterize(_c1 >= 0 ? ADC_UNIT_1 : ADC_UNIT_2, _atten,
                                     _width, DEFAULT_VREF, _adcChars);
          }
          *mv = esp_adc_cal_raw_to_voltage(value, _adcChars);
        }
        return ESP_OK;
      }
      esp_err_t range(int &min, int &max, uint32_t *mvMin = nullptr,
                      uint32_t *mvMax = nullptr) override {
        min = 0;
        switch (_width) {
          case ADC_WIDTH_BIT_9:
            max = (1 << 9) - 1;
            break;
          case ADC_WIDTH_BIT_10:
            max = (1 << 10) - 1;
            break;
          case ADC_WIDTH_BIT_11:
            max = (1 << 11) - 1;
            break;
          case ADC_WIDTH_BIT_12:
            max = (1 << 12) - 1;
            break;
          default:
            return ESP_FAIL;
        }
#if CONFIG_IDF_TARGET_ESP32S2
        if (mvMin || mvMax) {
          if (mvMin)
            *mvMin = 0;
          if (mvMax)
            switch (_atten) {
              case ADC_ATTEN_DB_0:
                *mvMax = 750;
                break;
              case ADC_ATTEN_DB_2_5:
                *mvMax = 1050;
                break;
              case ADC_ATTEN_DB_6:
                *mvMax = 1300;
                break;
              case ADC_ATTEN_DB_11:
                *mvMax = 2500;
                break;
              default:
                return ESP_FAIL;
            }
        }
#else
        if (mvMin || mvMax)
          switch (_atten) {
            case ADC_ATTEN_DB_0:
              if (mvMin)
                *mvMin = 100;
              if (mvMax)
                *mvMax = 950;
              break;
            case ADC_ATTEN_DB_2_5:
              if (mvMin)
                *mvMin = 100;
              if (mvMax)
                *mvMax = 1250;
              break;
            case ADC_ATTEN_DB_6:
              if (mvMin)
                *mvMin = 150;
              if (mvMax)
                *mvMax = 1750;
              break;
            case ADC_ATTEN_DB_11:
              if (mvMin)
                *mvMin = 150;
              if (mvMax)
                *mvMax = 2450;
              break;
            default:
              return ESP_FAIL;
          }
#endif
        return ESP_OK;
      }
      adc_atten_t getAtten() override {
        return _atten;
      }
      esp_err_t setAtten(adc_atten_t atten) override {
        if (atten == _atten)
          return ESP_OK;
        if (_c1 >= 0)
          ESP_CHECK_RETURN(
              adc1_config_channel_atten((adc1_channel_t)_c1, atten));
        else
          ESP_CHECK_RETURN(
              adc2_config_channel_atten((adc2_channel_t)_c2, atten));
        _atten = atten;
        _flags |= ADCFlags::CharsDirty;
        return ESP_OK;
      }
      adc_bits_width_t getWidth() override {
        return _width;
      }

      esp_err_t setWidth(adc_bits_width_t width) override {
        if (width == _width)
          return ESP_OK;
        if (_c1 >= 0)
          ESP_CHECK_RETURN(adc1_config_width(_width));
        _width = width;
        _flags |= ADCFlags::CharsDirty;
        return ESP_OK;
      }

     private:
      gpio_num_t _pin;
      int _c1, _c2;
      adc_bits_width_t _width = ADC_WIDTH_BIT_12;
      adc_atten_t _atten = ADC_ATTEN_DB_11;
      esp_adc_cal_characteristics_t *_adcChars = nullptr;
      ADCFlags _flags = ADCFlags::None;
      esp_err_t init() {
        if (!io::gpio2Adc(_pin, _c1, _c2))
          return ESP_FAIL;
        ESP_CHECK_RETURN(adc_gpio_init(_c1 >= 0 ? ADC_UNIT_1 : ADC_UNIT_2,
                                       (adc_channel_t)(_c1 >= 0 ? _c1 : _c2)));
        if (_c1 >= 0) {
          ESP_CHECK_RETURN(adc1_config_width(_width));
          ESP_CHECK_RETURN(
              adc1_config_channel_atten((adc1_channel_t)_c1, _atten));
        } else
          ESP_CHECK_RETURN(
              adc2_config_channel_atten((adc2_channel_t)_c2, _atten));
        return ESP_OK;
      }
    };

    uint8_t _pcntUnits = 0;

    class Pcnt : public io::pin::IPcnt {
     public:
      Pcnt(gpio_num_t pin) {
        memset(&_config, 0, sizeof(pcnt_config_t));
        _config.pulse_gpio_num = pin;
        _config.ctrl_gpio_num = -1;
        _config.neg_mode = PCNT_COUNT_INC;
        _config.counter_h_lim = 32767;
        bool foundFree = false;
        for (auto i = 0; i < 8; i++)
          if ((_pcntUnits & (1 << i)) == 0) {
            _config.unit = (pcnt_unit_t)i;
            foundFree = true;
          }
        if (!foundFree)
          return;
        _valid = update() == ESP_OK;
        if (_valid)
          _pcntUnits |= (1 << _config.unit);
      }
      ~Pcnt() override {
        if (_valid)
          _pcntUnits &= ~(1 << _config.unit);
      }
      bool valid() {
        return _valid;
      }
      esp_err_t read(int &value, bool reset = false) override {
        int16_t c;
        ESP_CHECK_RETURN(pcnt_get_counter_value(_config.unit, &c));
        value = c;
        if (reset)
          ESP_CHECK_RETURN(pcnt_counter_clear(_config.unit));
        return ESP_OK;
      }
      esp_err_t setLimits(int16_t min, int16_t max) override {
        _config.counter_l_lim = min;
        _config.counter_h_lim = max;
        return update();
      };
      esp_err_t setMode(pcnt_count_mode_t pos, pcnt_count_mode_t neg) override {
        _config.pos_mode = pos;
        _config.neg_mode = neg;
        return update();
      };
      esp_err_t setFilter(uint16_t filter) override {
        _filter = filter;
        return update();
      };

     private:
      uint16_t _filter = 0;
      bool _valid = false;
      pcnt_config_t _config;
      esp_err_t update() {
        ESP_CHECK_RETURN(pcnt_unit_config(&_config));
        if (_filter > 0 && _filter < 1024) {
          ESP_CHECK_RETURN(pcnt_filter_enable(_config.unit));
          ESP_CHECK_RETURN(pcnt_set_filter_value(_config.unit, _filter));
        } else {
          ESP_CHECK_RETURN(pcnt_filter_disable(_config.unit));
        }
        ESP_CHECK_RETURN(pcnt_counter_pause(_config.unit));
        ESP_CHECK_RETURN(pcnt_counter_clear(_config.unit));
        ESP_CHECK_RETURN(pcnt_counter_resume(_config.unit));
        return ESP_OK;
      }
    };

    class Pin : public io::IPin {
     public:
      Pin(int id) : IPin(id) {
        gpio_num_t num = this->num();
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_reset_pin(num));
        _features = Features::None;
        if (num < GPIO_NUM_6 || num > GPIO_NUM_11) {
          if ((num >= GPIO_NUM_2 && num <= GPIO_NUM_5) || num >= GPIO_NUM_12)
            _features |= Features::DigitalInput | Features::PulseCounter;
          if ((num != GPIO_NUM_3 && num <= GPIO_NUM_5) ||
              (num >= GPIO_NUM_12 && num <= GPIO_NUM_33)) {
            _features |= Features::DigitalOutput | Features::PullUp;
            if (num != GPIO_NUM_3)
              _features |= Features::PullDown;
          }
          int c1, c2;
          if (io::gpio2Adc(num, c1, c2))
            _features |= Features::ADC;
        }
      };
      gpio_num_t num() {
        return (gpio_num_t)(_id - io::Gpio::instance().pinBase());
      }
      Features features() override {
        return _features;
      }
      esp_err_t setDirection(gpio_mode_t mode) override {
        return gpio_set_direction(num(), mode);
      }
      esp_err_t setPull(gpio_pull_mode_t pull) override {
        return gpio_set_pull_mode(num(), pull);
      }
      esp_err_t digitalRead(bool &value) override {
        value = gpio_get_level(num()) != 0;
        return ESP_OK;
      }
      esp_err_t digitalWrite(bool value) override {
        return gpio_set_level(num(), value);
      }

      io::pin::Impl *createImpl(io::pin::Type type) {
        switch (type) {
          case io::pin::Type::ADC:
            if ((_features & io::IPin::Features::ADC) != 0)
              return new gpio::ADC(num());
            break;
          case io::pin::Type::Pcnt:
            if ((_features & io::IPin::Features::PulseCounter) != 0)
              return new gpio::Pcnt(num());
            break;
          default:
            return nullptr;
        }
        return nullptr;
      }

     private:
      Features _features;
    };
  }  // namespace gpio

  namespace io {
    IPin *Gpio::pin(int num) {
      if (num < 0 || num >= GPIO_NUM_MAX)
        return nullptr;
      auto id = num + _pinBase;
      std::lock_guard guard(_pinsMutex);
      auto pin = _pins.find(id);
      if (pin == _pins.end()) {
        auto gpio = new gpio::Pin(id);
        _pins[id] = gpio;
        return gpio;
      }
      return pin->second;
    }
    Gpio &Gpio::instance() {
      static Gpio i;
      return i;
    }

    Gpio::Gpio() : IPins(GPIO_NUM_MAX) {}
  }  // namespace io

  namespace gpio {
    io::IPin *pin(gpio_num_t n) {
      return io::Gpio::instance().pin(n);
    }
  }  // namespace gpio
}  // namespace esp32m