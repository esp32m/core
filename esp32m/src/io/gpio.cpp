#include "esp32m/io/gpio.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/io/pins.hpp"
#include "esp32m/io/utils.hpp"

#include <driver/adc.h>
#include <driver/dac.h>
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>
#include <esp_adc_cal.h>
#include <sdkconfig.h>
#include <soc/sens_reg.h>
#include <string.h>
#include <mutex>

#define DEFAULT_VREF \
  1100  // Use adc2_vref_to_gpio() to obtain a better
        // estimate

namespace esp32m {
  namespace gpio {
    enum ADCFlags {
      None = 0,
      Valid = BIT0,
      CharsDirty = BIT1,
    };

    ENUM_FLAG_OPERATORS(ADCFlags)

    class Pin : public io::IPin {
     public:
      Pin(int id);
      const char *name() const override {
        return _name;
      }
      gpio_num_t num();
      Features features() override {
        return _features;
      }
      void reset() override;
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
      esp_err_t attach(io::pin::ISR isr, gpio_int_type_t type) override;
      esp_err_t detach() override;

     protected:
      io::pin::Impl *createImpl(io::pin::Type type) override;

     private:
      Features _features;
      char _name[5];
      io::pin::ISR _isr = nullptr;
      void isr();
    };

    uint64_t adc2_ctrl = 0;
    class ADC : public io::pin::IADC {
     public:
      ADC(Pin *pin) : _pin(pin) {
        if (init() == ESP_OK)
          _flags |= ADCFlags::Valid;
      }
      bool valid() override {
        return (_flags & ADCFlags::Valid) != 0;
      }
      esp_err_t read(int &value, uint32_t *mv) override {
        esp_err_t err = ESP_OK;
        adc_power_acquire();
        if (_c1 >= 0) {
          int v = adc1_get_raw((adc1_channel_t)_c1);
          if (v < 0)
            err = ESP_FAIL;
          else
            value = v;
        } else {
          WRITE_PERI_REG(SENS_SAR_READ_CTRL2_REG, adc2_ctrl);
          SET_PERI_REG_MASK(SENS_SAR_READ_CTRL2_REG, SENS_SAR2_DATA_INV);
          err = adc2_get_raw((adc2_channel_t)_c2, _width, &value);
        }
        adc_power_release();
        ESP_CHECK_RETURN(err);
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
      Pin *_pin;
      int _c1, _c2;
      adc_bits_width_t _width = ADC_WIDTH_BIT_12;
      adc_atten_t _atten = ADC_ATTEN_DB_11;
      esp_adc_cal_characteristics_t *_adcChars = nullptr;
      ADCFlags _flags = ADCFlags::None;
      esp_err_t init() {
        if (!io::gpio2Adc(_pin->num(), _c1, _c2))
          return ESP_FAIL;
        /*ESP_CHECK_RETURN(adc_gpio_init(_c1 >= 0 ? ADC_UNIT_1 : ADC_UNIT_2,
                                       (adc_channel_t)(_c1 >= 0 ? _c1 :
           _c2)));*/
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
    class DAC : public io::pin::IDAC {
     public:
      DAC(Pin *pin) {
        switch (pin->num()) {
          case DAC_CHANNEL_1_GPIO_NUM:
            _channel = DAC_CHANNEL_1;
            break;
          case DAC_CHANNEL_2_GPIO_NUM:
            _channel = DAC_CHANNEL_2;
            break;
          default:
            _channel = DAC_CHANNEL_MAX;
            break;
        }
      }
      bool valid() override {
        return _channel < DAC_CHANNEL_MAX;
      }
      esp_err_t write(float value) override {
        if (value >= 0) {
          ESP_CHECK_RETURN(dac_output_enable(_channel));
          ESP_CHECK_RETURN(dac_output_voltage(_channel, 255 * value));
        } else
          ESP_CHECK_RETURN(dac_output_disable(_channel));
        return ESP_OK;
      }

     private:
      dac_channel_t _channel;
    };

    class Pcnt : public io::pin::IPcnt {
     public:
      Pcnt(Pin *pin) : _pin(pin) {
        _valid = install() == ESP_OK;
      }
      ~Pcnt() override {
        if (_channel && _unit)
          pcnt_unit_stop(_unit);
        if (_channel) {
          pcnt_del_channel(_channel);
          _channel = nullptr;
        }
        if (_unit) {
          pcnt_del_unit(_unit);
          _unit = nullptr;
        }
      }
      bool valid() override {
        return _valid;
      }
      esp_err_t read(int &value, bool reset = false) override {
        int c;
        ESP_CHECK_RETURN(pcnt_unit_get_count(_unit, &c));
        value = c;
        if (reset)
          ESP_CHECK_RETURN(pcnt_unit_clear_count(_unit));
        return ESP_OK;
      }
      /*esp_err_t setLimits(int16_t min, int16_t max) override {
        return ESP_OK;
      };*/
      esp_err_t setMode(pcnt_channel_edge_action_t pos,
                        pcnt_channel_edge_action_t neg) override {
        return pcnt_channel_set_edge_action(_channel, pos, neg);
      };
      esp_err_t setFilter(uint16_t filter) override {
        pcnt_glitch_filter_config_t filter_config = {
            .max_glitch_ns = filter,
        };
        ESP_CHECK_RETURN(pcnt_unit_set_glitch_filter(_unit, &filter_config));
        return ESP_OK;
      };

     private:
      Pin *_pin;
      pcnt_unit_handle_t _unit = nullptr;
      pcnt_channel_handle_t _channel = nullptr;
      bool _valid = false;
      esp_err_t install() {
        pcnt_unit_config_t unit_config = {
            .low_limit = 0,
            .high_limit = 32767,
        };
        ESP_CHECK_RETURN(pcnt_new_unit(&unit_config, &_unit));
        pcnt_chan_config_t chan_config = {
            .edge_gpio_num = _pin->num(),
            .level_gpio_num = -1,
            .flags = {},
        };
        ESP_CHECK_RETURN(pcnt_new_channel(_unit, &chan_config, &_channel));
        ESP_CHECK_RETURN(pcnt_unit_start(_unit));
        return ESP_OK;
      };
    };

    struct LedcTimer {
      ledc_timer_config_t c;
      int ref;
    };

    LedcTimer *ledcTimers[LEDC_TIMER_MAX] = {};
    std::mutex ledcTimersMutex;
    uint8_t _ledcUnits = 0;

    bool ledcTimersEqual(LedcTimer *a, LedcTimer *b) {
      if (!a || !b)
        return false;
      return a->c.speed_mode == b->c.speed_mode &&
             a->c.duty_resolution == b->c.duty_resolution &&
             a->c.freq_hz == b->c.freq_hz && a->c.clk_cfg == b->c.clk_cfg;
    }
    void ledcTimerRef(int timer) {
      if (timer >= 0 && timer < LEDC_TIMER_MAX && ledcTimers[timer])
        ledcTimers[timer]->ref++;
    }
    void ledcTimerUnref(int timer) {
      if (timer >= 0 && timer < LEDC_TIMER_MAX && ledcTimers[timer])
        ledcTimers[timer]->ref--;
    }
    int ledcTimerGet(ledc_mode_t speed_mode, ledc_timer_bit_t duty_resolution,
                     uint32_t freq_hz, ledc_clk_cfg_t clk_cfg) {
      LedcTimer t = {.c =
                         {
                             .speed_mode = speed_mode,
                             .duty_resolution = duty_resolution,
                             .timer_num = LEDC_TIMER_MAX,
                             .freq_hz = freq_hz,
                             .clk_cfg = clk_cfg,
                         },
                     .ref = 0};
      int freeSlot = -1;
      for (int i = 0; i < LEDC_TIMER_MAX; i++)
        if (ledcTimersEqual(&t, ledcTimers[i]))
          return i;
        else if (freeSlot == -1) {
          if (ledcTimers[i] == nullptr)
            freeSlot = i;
          else if (!ledcTimers[i]->ref) {
            delete ledcTimers[i];
            ledcTimers[i] = nullptr;
            freeSlot = i;
          }
        }
      if (freeSlot != -1) {
        t.c.timer_num = (ledc_timer_t)freeSlot;
        ledcTimers[freeSlot] = new LedcTimer(t);
      }
      return freeSlot;
    }

    class LEDC : public io::pin::ILEDC {
     public:
      LEDC(Pin *pin) : _pin(pin) {
        std::lock_guard lock(ledcTimersMutex);
        for (auto i = 0; i < LEDC_CHANNEL_MAX; i++)
          if ((_ledcUnits & (1 << i)) == 0) {
            _channel = i;
            break;
          }
        if (_channel < 0)
          return;
        if (valid())
          _ledcUnits |= (1 << _channel);
      }
      ~LEDC() override {
        std::lock_guard lock(ledcTimersMutex);
        if (valid())
          _ledcUnits &= ~(1 << _channel);
        ledcTimerUnref(_timer);
      }
      bool valid() override {
        return _channel >= 0;
      }
      esp_err_t config(uint32_t freq_hz, ledc_mode_t mode,
                       ledc_timer_bit_t duty_resolution,
                       ledc_clk_cfg_t clk_cfg) override {
        if (!valid())
          return ESP_FAIL;
        std::lock_guard lock(ledcTimersMutex);
        ledcTimerUnref(_timer);
        _timer = ledcTimerGet(mode, duty_resolution, freq_hz, clk_cfg);
        if (_timer == -1)
          return ESP_FAIL;
        _speedMode = mode;
        ledcTimerRef(_timer);
        ESP_CHECK_RETURN(ledc_timer_config(&ledcTimers[_timer]->c));
        ledc_channel_config_t ledc_conf = {
            .gpio_num = _pin->num(),
            .speed_mode = mode,
            .channel = (ledc_channel_t)_channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = (ledc_timer_t)_timer,
            .duty = 0x0,
            .hpoint = 0,
            .flags =
                {
                    .output_invert = 0,
                },
        };
        ESP_CHECK_RETURN(ledc_channel_config(&ledc_conf));
        LOGI(_pin, "LEDC config: channel=%i, timer=%i, freq=%i, duty_res=%i",
             _channel, _timer, freq_hz, duty_resolution);
        return ESP_OK;
      }
      esp_err_t setDuty(uint32_t duty) override {
        if (!valid())
          return ESP_FAIL;
        ESP_CHECK_RETURN(
            ledc_set_duty(_speedMode, (ledc_channel_t)_channel, duty));
        ESP_CHECK_RETURN(
            ledc_update_duty(_speedMode, (ledc_channel_t)_channel));
        LOGI(_pin, "LEDC set duty %i", duty);
        return ESP_OK;
      }

     private:
      Pin *_pin;
      ledc_mode_t _speedMode = LEDC_HIGH_SPEED_MODE;
      int _channel = -1;
      int _timer = -1;
    };

    Pin::Pin(int id) : IPin(id) {
      gpio_num_t num = this->num();
      snprintf(_name, sizeof(_name), "IO%02u", num);
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
        if (num == DAC_CHANNEL_1_GPIO_NUM || num == DAC_CHANNEL_2_GPIO_NUM)
          _features |= Features::DAC;
        int c1, c2;
        if (io::gpio2Adc(num, c1, c2))
          _features |= Features::ADC;
      }
    };
    gpio_num_t Pin::num() {
      return (gpio_num_t)(_id - io::Gpio::instance().pinBase());
    }

    void Pin::reset() {
      IPin::reset();
      detach();
      ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_reset_pin(num()));
    };

    esp_err_t Pin::attach(io::pin::ISR isr, gpio_int_type_t type) {
      std::lock_guard guard(_mutex);
      if (_isr)
        return ESP_ERR_INVALID_STATE;
      esp_err_t err = gpio_install_isr_service(0);
      if (err != ESP_ERR_INVALID_STATE)
        ESP_CHECK_RETURN(err);
      ESP_CHECK_RETURN(gpio_set_intr_type(num(), type));
      ESP_CHECK_RETURN(gpio_intr_enable(num()));
      _isr = isr;
      err = gpio_isr_handler_add(
          num(), [](void *self) { ((Pin *)self)->isr(); }, this);
      if (err != ESP_OK) {
        _isr = nullptr;
        return err;
      }
      return ESP_OK;
    }

    esp_err_t Pin::detach() {
      std::lock_guard guard(_mutex);
      ESP_CHECK_RETURN(gpio_intr_disable(num()));
      if (!_isr)
        return ESP_ERR_INVALID_STATE;
      ESP_CHECK_RETURN(gpio_isr_handler_remove(num()));
      _isr = nullptr;
      return ESP_OK;
    }

    void Pin::isr() {
      io::pin::ISR isr;
      {
        std::lock_guard guard(_mutex);
        isr = _isr;
      }
      if (isr)
        isr();
    }

    io::pin::Impl *Pin::createImpl(io::pin::Type type) {
      switch (type) {
        case io::pin::Type::ADC:
          if ((_features & io::IPin::Features::ADC) != 0)
            return new gpio::ADC(this);
          break;
        case io::pin::Type::DAC:
          if ((_features & io::IPin::Features::DAC) != 0)
            return new gpio::DAC(this);
          break;
        case io::pin::Type::Pcnt:
          if ((_features & io::IPin::Features::PulseCounter) != 0)
            return new gpio::Pcnt(this);
          break;
        case io::pin::Type::LEDC:
          if ((_features & io::IPin::Features::DigitalOutput) != 0)
            return new gpio::LEDC(this);
          break;
        default:
          return nullptr;
      }
      return nullptr;
    }
  }  // namespace gpio

  namespace io {
    IPin *Gpio::newPin(int id) {
      return new gpio::Pin(id);
    }

    Gpio &Gpio::instance() {
      static Gpio i;
      return i;
    }

    Gpio::Gpio() {
      init(GPIO_NUM_MAX);
      gpio::adc2_ctrl = READ_PERI_REG(SENS_SAR_READ_CTRL2_REG);
    }
  }  // namespace io

  namespace gpio {
    io::IPin *pin(gpio_num_t n) {
      return io::Gpio::instance().pin(n);
    }
  }  // namespace gpio
}  // namespace esp32m