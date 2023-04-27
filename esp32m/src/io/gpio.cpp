#include "esp32m/io/gpio.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/io/pins.hpp"
#include "esp32m/io/utils.hpp"

#include <driver/dac_oneshot.h>
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_timer.h>
#include <limits.h>
#include <sdkconfig.h>
#include <soc/sens_reg.h>
#include <string.h>
#include <mutex>
#if SOC_DAC_SUPPORTED
#  include <soc/dac_channel.h>
#endif

#define DEFAULT_VREF \
  1100  // Use adc2_vref_to_gpio() to obtain a better
        // estimate

namespace esp32m {
  namespace gpio {

    adc_cali_scheme_ver_t calischeme = (adc_cali_scheme_ver_t)0;

    enum ADCFlags {
      None = 0,
      Valid = BIT0,
      CharsDirty = BIT1,
    };

    ENUM_FLAG_OPERATORS(ADCFlags)
    class ISRArg;

    class Pin : public io::IPin {
     public:
      Pin(int id);
      const char *name() const override {
        return _name;
      }
      gpio_num_t num();
      io::pin::Features features() override {
        return _features;
      }
      void reset() override;

     protected:
      io::pin::Impl *createImpl(io::pin::Type type) override;

     private:
      io::pin::Features _features;
      char _name[5];
    };

    class Digital : public io::pin::IDigital {
     public:
      Digital(Pin *pin) : _pin(pin) {}
      ~Digital() override {
        detach();
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_reset_pin(_pin->num()));
      }

      esp_err_t setDirection(gpio_mode_t mode) override {
        return gpio_set_direction(_pin->num(), mode);
      }
      esp_err_t setPull(gpio_pull_mode_t pull) override {
        return gpio_set_pull_mode(_pin->num(), pull);
      }
      esp_err_t read(bool &value) override {
        value = gpio_get_level(_pin->num()) != 0;
        return ESP_OK;
      }
      esp_err_t write(bool value) override {
        return gpio_set_level(_pin->num(), value);
      }
      esp_err_t attach(QueueHandle_t queue, gpio_int_type_t type) override;
      esp_err_t detach() override;

     private:
      Pin *_pin;
      ISRArg *_isr = nullptr;
      friend class ISRArg;
    };

    class ISRArg {
     public:
      ISRArg(Digital *pin, QueueHandle_t queue) : _pin(pin), _queue(queue) {
        _num = pin->_pin->num();
        _stamp = esp_timer_get_time();
        _level = gpio_get_level(_num) != 0;
      }

     private:
      Digital *_pin;
      gpio_num_t _num;
      QueueHandle_t _queue;
      bool _level;
      int64_t _stamp;
      friend IRAM_ATTR void gpio_isr_handler(void *self);
    };

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
        if (!valid())
          return ESP_FAIL;
        adc_oneshot_unit_handle_t handle;
        ESP_CHECK_RETURN(getADCUnitHandle(_unit, &handle));
        /*
        esp_err_t err = ESP_OK;
        for (int attempt = 0; attempt < 3; attempt++) {
          // unfortunately it doesnt't wait for the other thread to release ADC,
        so we have to do ugly retries.
          // we can add our own mutex, but what if
          err = adc_oneshot_read(handle, _channel, &value);
          if (err == ESP_ERR_TIMEOUT)
            delay(attempt);
          else
            break;
        }
        ESP_CHECK_RETURN(err);
        */
        auto &mutex = getADCUnitMutex(_unit);
        {
          std::lock_guard guard(mutex);
          ESP_CHECK_RETURN(adc_oneshot_read(handle, _channel, &value));
        }
        if (mv) {
          if (!_calihandle || (_flags & ADCFlags::CharsDirty) != 0) {
            _flags &= ~ADCFlags::CharsDirty;
            if (_calihandle) {
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
              if ((calischeme & ADC_CALI_SCHEME_VER_LINE_FITTING) != 0)
                adc_cali_delete_scheme_line_fitting(_calihandle);
#endif
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
              if ((calischeme & ADC_CALI_SCHEME_VER_CURVE_FITTING) != 0)
                adc_cali_delete_scheme_curve_fitting(_calihandle);
#endif
            }
            _calihandle = nullptr;

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
            if ((calischeme & ADC_CALI_SCHEME_VER_LINE_FITTING) != 0) {
              adc_cali_line_fitting_config_t cali_config = {
                  .unit_id = _unit,
                  .atten = _atten,
                  .bitwidth = _width,
                  .default_vref = DEFAULT_VREF};
              ESP_CHECK_RETURN(adc_cali_create_scheme_line_fitting(
                  &cali_config, &_calihandle));
            }
#endif
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
            if (!_calihandle &&
                (calischeme & ADC_CALI_SCHEME_VER_CURVE_FITTING) != 0) {
              adc_cali_curve_fitting_config_t cali_config = {
                  .unit_id = _unit,
                  .atten = _atten,
                  .bitwidth = _width,
              };
              ESP_CHECK_RETURN(adc_cali_create_scheme_curve_fitting(
                  &cali_config, &_calihandle));
            }
#endif
          }
          if (_calihandle) {
            int v;
            ESP_CHECK_RETURN(adc_cali_raw_to_voltage(_calihandle, value, &v));
            *mv = (uint32_t)v;
          }
        }
        return ESP_OK;
      }
      esp_err_t range(int &min, int &max, uint32_t *mvMin = nullptr,
                      uint32_t *mvMax = nullptr) override {
        min = 0;
        auto width = getWidth();
        if (width <= 0)
          return ESP_FAIL;
        max = (1 << width) - 1;
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
        _atten = atten;
        _flags |= ADCFlags::CharsDirty;
        return update();
      }
      int getWidth() override {
        return _width == ADC_BITWIDTH_DEFAULT ? SOC_ADC_RTC_MAX_BITWIDTH
                                              : _width;
      }

      esp_err_t setWidth(adc_bitwidth_t width) override {
        if (width == _width)
          return ESP_OK;
        _width = width;
        _flags |= ADCFlags::CharsDirty;
        return update();
      }

     private:
      Pin *_pin;
      adc_unit_t _unit;
      adc_channel_t _channel;
      adc_cali_handle_t _calihandle = nullptr;
      adc_bitwidth_t _width = ADC_BITWIDTH_DEFAULT;
      adc_atten_t _atten = ADC_ATTEN_DB_11;
      ADCFlags _flags = ADCFlags::None;
      esp_err_t init() {
        if (!calischeme)
          ESP_ERROR_CHECK_WITHOUT_ABORT(adc_cali_check_scheme(&calischeme));
        ESP_CHECK_RETURN(
            adc_oneshot_io_to_channel(_pin->num(), &_unit, &_channel));
        /*logd("initialized oneshot ADC on gpio %d, ADC%d, channel %d",
             _pin->num(), _unit + 1, _channel);*/
        return update();
      }
      esp_err_t update() {
        adc_oneshot_chan_cfg_t ccfg = {
            .atten = _atten,
            .bitwidth = _width,
        };
        adc_oneshot_unit_handle_t handle;
        ESP_CHECK_RETURN(getADCUnitHandle(_unit, &handle));
        ESP_CHECK_RETURN(adc_oneshot_config_channel(handle, _channel, &ccfg));
        return ESP_OK;
      }
    };

#if SOC_DAC_SUPPORTED
    class DAC : public io::pin::IDAC {
     public:
      DAC(Pin *pin) {
        switch (pin->num()) {
          case DAC_CHANNEL_1_GPIO_NUM:
            _channel = DAC_CHAN_0;
            break;
          case DAC_CHANNEL_2_GPIO_NUM:
            _channel = DAC_CHAN_1;
            break;
          default:
            _channel = (dac_channel_t)-1;
            break;
        }
      }
      bool valid() override {
        return _channel >= 0;
      }
      esp_err_t write(float value) override {
        if (value >= 0) {
          if (!_handle) {
            dac_oneshot_config_t cfg = {.chan_id = _channel};
            ESP_CHECK_RETURN(dac_oneshot_new_channel(&cfg, &_handle));
          }
          ESP_CHECK_RETURN(dac_oneshot_output_voltage(_handle, 255 * value));
        }
        return ESP_OK;
      }

     private:
      dac_channel_t _channel;
      dac_oneshot_handle_t _handle = nullptr;
    };
#endif
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
          pcnt_unit_disable(_unit);
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
        ESP_CHECK_RETURN(pcnt_unit_stop(_unit));
        ESP_CHECK_RETURN(pcnt_unit_disable(_unit));
        ESP_CHECK_RETURN(pcnt_unit_set_glitch_filter(_unit, &filter_config));
        ESP_CHECK_RETURN(pcnt_unit_enable(_unit));
        ESP_CHECK_RETURN(pcnt_unit_clear_count(_unit));
        ESP_CHECK_RETURN(pcnt_unit_start(_unit));
        return ESP_OK;
      };

     private:
      Pin *_pin;
      pcnt_unit_handle_t _unit = nullptr;
      pcnt_channel_handle_t _channel = nullptr;
      bool _valid = false;
      esp_err_t install() {
        pcnt_unit_config_t unit_config = {.low_limit = SHRT_MIN,
                                          .high_limit = SHRT_MAX,
                                          .flags = {
                                              .accum_count = true,
                                          }};
        ESP_CHECK_RETURN(pcnt_new_unit(&unit_config, &_unit));
        pcnt_chan_config_t chan_config = {
            .edge_gpio_num = _pin->num(),
            .level_gpio_num = -1,
            .flags = {},
        };
        ESP_CHECK_RETURN(pcnt_new_channel(_unit, &chan_config, &_channel));
        ESP_CHECK_RETURN(pcnt_unit_enable(_unit));
        ESP_CHECK_RETURN(pcnt_unit_clear_count(_unit));
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
      LedcTimer t = {};
      t.c.speed_mode = speed_mode;
      t.c.duty_resolution = duty_resolution;
      t.c.timer_num = LEDC_TIMER_MAX;
      t.c.freq_hz = freq_hz;
      t.c.clk_cfg = clk_cfg;
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
#if SOC_LEDC_SUPPORT_HS_MODE
      ledc_mode_t _speedMode = LEDC_HIGH_SPEED_MODE;
#else
      ledc_mode_t _speedMode = LEDC_LOW_SPEED_MODE;
#endif
      int _channel = -1;
      int _timer = -1;
    };

    Pin::Pin(int id) : IPin(id) {
      gpio_num_t num = this->num();
      snprintf(_name, sizeof(_name), "IO%02u", num);
      _features = io::pin::Features::None;
      if (!GPIO_IS_VALID_GPIO(num))
        return;
      if (GPIO_IS_VALID_DIGITAL_IO_PAD(num))
        _features |=
            io::pin::Features::DigitalInput | io::pin::Features::PulseCounter;
      if (GPIO_IS_VALID_OUTPUT_GPIO(num))
        _features |= io::pin::Features::DigitalOutput |
                     io::pin::Features::PullUp | io::pin::Features::PullDown;
#if SOC_DAC_SUPPORTED
      if (num == DAC_CHANNEL_1_GPIO_NUM || num == DAC_CHANNEL_2_GPIO_NUM)
        _features |= io::pin::Features::DAC;
#endif
      adc_unit_t unit;
      adc_channel_t channel;
      if (adc_oneshot_io_to_channel(num, &unit, &channel) == ESP_OK)
        _features |= io::pin::Features::ADC;
      ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_reset_pin(num));
    };
    gpio_num_t Pin::num() {
      return (gpio_num_t)(_id - io::Gpio::instance().pinBase());
    }

    void Pin::reset() {
      IPin::reset();
    };

    void IRAM_ATTR gpio_isr_handler(void *self) {
      int64_t stamp = esp_timer_get_time();
      auto arg = (ISRArg *)self;
      bool level = gpio_get_level(arg->_num) != 0;
      if (arg->_level == level)
        return;
      int64_t diff = stamp - arg->_stamp;
      if (diff > 0x7FFFFFFF)
        diff = 0x7FFFFFFF;
      if (level)
        diff = -diff;
      arg->_level = level;
      arg->_stamp = stamp;
      BaseType_t high_task_wakeup = pdFALSE;
      xQueueSendFromISR(arg->_queue, &diff, &high_task_wakeup);
      if (high_task_wakeup != pdFALSE)  // TODO: should we do this? many
                                        // examples seem to work without it
        portYIELD_FROM_ISR();
    }

    esp_err_t Digital::attach(QueueHandle_t queue, gpio_int_type_t type) {
      if (!queue)
        return ESP_FAIL;
      std::lock_guard guard(_pin->mutex());
      if (_isr)
        return ESP_ERR_INVALID_STATE;
      esp_err_t err = gpio_install_isr_service(0);
      if (err != ESP_ERR_INVALID_STATE)
        ESP_CHECK_RETURN(err);
      auto num = _pin->num();
      gpio_config_t gpio_conf;
      gpio_conf.intr_type = type;
      gpio_conf.mode = GPIO_MODE_INPUT;
      gpio_conf.pin_bit_mask = (1ULL << num);
      gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
      gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
      gpio_config(&gpio_conf);

      // ESP_CHECK_RETURN(gpio_set_intr_type(num(), type));
      // ESP_CHECK_RETURN(gpio_intr_enable(num()));
      _isr = new ISRArg(this, queue);
      err = gpio_isr_handler_add(num, gpio_isr_handler, _isr);
      if (err != ESP_OK) {
        delete _isr;
        _isr = nullptr;
        return err;
      }
      return ESP_OK;
    }

    esp_err_t Digital::detach() {
      std::lock_guard guard(_pin->mutex());
      auto n = _pin->num();
      ESP_CHECK_RETURN(gpio_intr_disable(n));
      if (!_isr)
        return ESP_ERR_INVALID_STATE;
      ESP_CHECK_RETURN(gpio_isr_handler_remove(n));
      delete _isr;
      _isr = nullptr;
      return ESP_OK;
    }

    io::pin::Impl *Pin::createImpl(io::pin::Type type) {
      switch (type) {
        case io::pin::Type::Digital:
          if ((_features & (io::pin::Features::DigitalInput |
                            io::pin::Features::DigitalOutput)) != 0)
            return new gpio::Digital(this);
          break;
        case io::pin::Type::ADC:
          if ((_features & io::pin::Features::ADC) != 0)
            return new gpio::ADC(this);
          break;
#if SOC_DAC_SUPPORTED
        case io::pin::Type::DAC:
          if ((_features & io::pin::Features::DAC) != 0)
            return new gpio::DAC(this);
          break;
#endif
        case io::pin::Type::Pcnt:
          if ((_features & io::pin::Features::PulseCounter) != 0)
            return new gpio::Pcnt(this);
          break;
        case io::pin::Type::LEDC:
          if ((_features & io::pin::Features::DigitalOutput) != 0)
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
    }
  }  // namespace io

  namespace gpio {
    io::IPin *pin(gpio_num_t n) {
      return io::Gpio::instance().pin(n);
    }

    adc_oneshot_unit_handle_t adc_handles[] = {nullptr, nullptr};
    std::map<adc_unit_t, std::mutex> _adc_locks;

    esp_err_t getADCUnitHandle(adc_unit_t unit,
                               adc_oneshot_unit_handle_t *handle) {
      auto h = adc_handles[unit];
      if (!h) {
        adc_oneshot_unit_init_cfg_t ucfg = {.unit_id = unit,
                                            .clk_src = (adc_oneshot_clk_src_t)0,
                                            .ulp_mode = ADC_ULP_MODE_DISABLE};
        ESP_CHECK_RETURN(adc_oneshot_new_unit(&ucfg, &h));
        adc_handles[unit] = h;
      }
      *handle = h;
      return ESP_OK;
    }

    std::mutex &getADCUnitMutex(adc_unit_t unit) {
      auto it = _adc_locks.find(unit);
      if (it != _adc_locks.end())
        return it->second;
      return _adc_locks[unit];
    }

  }  // namespace gpio
}  // namespace esp32m