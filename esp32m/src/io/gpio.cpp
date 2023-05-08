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

    using namespace io;

    class ISRArg;

    class Pin : public IPin {
     public:
      Pin(int id);
      const char *name() const override {
        return _name;
      }
      gpio_num_t num();
      pin::Flags flags() override {
        return _features;
      }
      void reset() override;

     protected:
      esp_err_t createFeature(pin::Type type, pin::Feature **feature) override;

     private:
      pin::Flags _features;
      char _name[5];
    };

    class Digital : public pin::IDigital {
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

    namespace adc {
      class Unit;
      static adc_cali_scheme_ver_t calischeme = (adc_cali_scheme_ver_t)0;
      static std::map<adc_unit_t, std::unique_ptr<Unit> > _units;

      enum Flags {
        None = 0,
        Dirty = BIT0,
        CharsDirty = BIT1,
      };

      ENUM_FLAG_OPERATORS(Flags)

      class Unit {
       public:
        Unit(const Unit &) = delete;
        ~Unit() {
          ESP_ERROR_CHECK_WITHOUT_ABORT(adc_oneshot_del_unit(_handle));
        }
        static esp_err_t claim(adc_unit_t unit, Unit **u) {
          if (!u)
            return ESP_ERR_INVALID_ARG;
          Unit *result = nullptr;
          auto it = _units.find(unit);
          if (it == _units.end()) {
            adc_oneshot_unit_init_cfg_t ucfg = {
                .unit_id = unit,
                .clk_src = (adc_oneshot_clk_src_t)0,
                .ulp_mode = ADC_ULP_MODE_DISABLE};
            adc_oneshot_unit_handle_t handle;
            ESP_CHECK_RETURN(adc_oneshot_new_unit(&ucfg, &handle));
            result = new Unit(unit, handle);
            _units[unit] = std::unique_ptr<Unit>(result);
          } else
            result = it->second.get();
          result->_refs++;
          *u = result;
          return ESP_OK;
        }
        void release() {
          if (_refs <= 0) {
            logw("invalid ADC unit release, %d refs", _refs);
            return;
          }
          _refs--;
          if (_refs == 0)
            _units.erase(_unit);
        }
        std::mutex &mutex() {
          return _mutex;
        }
        adc_oneshot_unit_handle_t handle() const {
          return _handle;
        }
        adc_unit_t unit() const {
          return _unit;
        }

       private:
        int _refs = 0;
        adc_unit_t _unit;
        adc_oneshot_unit_handle_t _handle;
        std::mutex _mutex;
        Unit(adc_unit_t unit, adc_oneshot_unit_handle_t handle)
            : _unit(unit), _handle(handle) {}
      };

    }  // namespace adc

    class ADC : public pin::IADC {
     public:
      ~ADC() override {
        _unit->release();
      }
      esp_err_t read(int &value, uint32_t *mv) override {
        ESP_CHECK_RETURN(update());
        {
          std::lock_guard guard(_unit->mutex());
          ESP_CHECK_RETURN(adc_oneshot_read(_unit->handle(), _channel, &value));
        }
        if (mv) {
          if (!_calihandle || (_flags & adc::Flags::CharsDirty) != 0) {
            _flags &= ~adc::Flags::CharsDirty;
            if (_calihandle) {
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
              if ((adc::calischeme & ADC_CALI_SCHEME_VER_LINE_FITTING) != 0)
                adc_cali_delete_scheme_line_fitting(_calihandle);
#endif
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
              if ((adc::calischeme & ADC_CALI_SCHEME_VER_CURVE_FITTING) != 0)
                adc_cali_delete_scheme_curve_fitting(_calihandle);
#endif
            }
            _calihandle = nullptr;

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
            if ((adc::calischeme & ADC_CALI_SCHEME_VER_LINE_FITTING) != 0) {
              adc_cali_line_fitting_config_t cali_config = {
                  .unit_id = _unit->unit(),
                  .atten = _atten,
                  .bitwidth = _width,
                  .default_vref = DEFAULT_VREF};
              ESP_CHECK_RETURN(adc_cali_create_scheme_line_fitting(
                  &cali_config, &_calihandle));
            }
#endif
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
            if (!_calihandle &&
                (adc::calischeme & ADC_CALI_SCHEME_VER_CURVE_FITTING) != 0) {
              adc_cali_curve_fitting_config_t cali_config = {
                  .unit_id = _unit->unit(),
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
        _flags |= adc::Flags::CharsDirty | adc::Flags::Dirty;
        return ESP_OK;
      }
      int getWidth() override {
        return _width == ADC_BITWIDTH_DEFAULT ? SOC_ADC_RTC_MAX_BITWIDTH
                                              : _width;
      }

      esp_err_t setWidth(adc_bitwidth_t width) override {
        if (width == _width)
          return ESP_OK;
        _width = width;
        _flags |= adc::Flags::CharsDirty | adc::Flags::Dirty;
        return ESP_OK;
      }

      static esp_err_t create(Pin *pin, pin::Feature **feature) {
        if (!adc::calischeme)
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              adc_cali_check_scheme(&adc::calischeme));
        adc_unit_t unit;
        adc_channel_t channel;
        ESP_CHECK_RETURN(
            adc_oneshot_io_to_channel(pin->num(), &unit, &channel));
        adc::Unit *u = nullptr;
        ESP_CHECK_RETURN(adc::Unit::claim(unit, &u));
        *feature = new ADC(pin, u, channel);
        /*logd("initialized oneshot ADC on gpio %d, ADC%d, channel %d",
             _pin->num(), _unit + 1, _channel);*/
        return ESP_OK;
      }

     private:
      Pin *_pin;
      adc::Unit *_unit;
      adc_channel_t _channel;
      adc_cali_handle_t _calihandle = nullptr;
      adc_bitwidth_t _width = ADC_BITWIDTH_DEFAULT;
      adc_atten_t _atten = ADC_ATTEN_DB_11;
      adc::Flags _flags = adc::Flags::Dirty;
      ADC(Pin *pin, adc::Unit *unit, adc_channel_t channel)
          : _pin(pin), _unit(unit), _channel(channel) {}
      esp_err_t update() {
        if ((_flags & adc::Flags::Dirty) != 0) {
          _flags &= ~adc::Flags::Dirty;

          adc_oneshot_chan_cfg_t ccfg = {
              .atten = _atten,
              .bitwidth = _width,
          };
          ESP_CHECK_RETURN(
              adc_oneshot_config_channel(_unit->handle(), _channel, &ccfg));
        }
        return ESP_OK;
      }
    };

#if SOC_DAC_SUPPORTED
    class DAC : public pin::IDAC {
     public:
      virtual ~DAC() {
        ESP_ERROR_CHECK_WITHOUT_ABORT(dac_oneshot_del_channel(_handle));
      }
      esp_err_t write(float value) override {
        if (value < 0)
          return ESP_ERR_INVALID_ARG;
        ESP_CHECK_RETURN(dac_oneshot_output_voltage(_handle, 255 * value));
        return ESP_OK;
      }

      static esp_err_t create(Pin *pin, pin::Feature **feature) {
        dac_channel_t channel;
        switch (pin->num()) {
          case DAC_CHANNEL_1_GPIO_NUM:
            channel = DAC_CHAN_0;
            break;
          case DAC_CHANNEL_2_GPIO_NUM:
            channel = DAC_CHAN_1;
            break;
          default:
            return ESP_ERR_INVALID_ARG;
        }
        dac_oneshot_handle_t handle;
        dac_oneshot_config_t cfg = {.chan_id = channel};
        ESP_CHECK_RETURN(dac_oneshot_new_channel(&cfg, &handle));
        *feature = new DAC(pin, channel, handle);
        return ESP_OK;
      }

     private:
      dac_channel_t _channel;
      dac_oneshot_handle_t _handle;
      DAC(Pin *pin, dac_channel_t channel, dac_oneshot_handle_t handle)
          : _channel(channel), _handle(handle) {}
    };
#endif
    class Pcnt : public pin::IPcnt {
     public:
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

      static esp_err_t create(Pin *pin, pin::Feature **feature) {
        pcnt_unit_config_t unit_config = {.low_limit = SHRT_MIN,
                                          .high_limit = SHRT_MAX,
                                          .flags = {
                                              .accum_count = true,
                                          }};
        pcnt_unit_handle_t unit;
        pcnt_channel_handle_t channel;
        ESP_CHECK_RETURN(pcnt_new_unit(&unit_config, &unit));
        pcnt_chan_config_t chan_config = {
            .edge_gpio_num = pin->num(),
            .level_gpio_num = -1,
            .flags = {},
        };
        ESP_CHECK_RETURN(pcnt_new_channel(unit, &chan_config, &channel));
        ESP_CHECK_RETURN(pcnt_unit_enable(unit));
        ESP_CHECK_RETURN(pcnt_unit_clear_count(unit));
        ESP_CHECK_RETURN(pcnt_unit_start(unit));
        *feature = new Pcnt(pin, unit, channel);
        return ESP_OK;
      };

     private:
      Pin *_pin;
      pcnt_unit_handle_t _unit;
      pcnt_channel_handle_t _channel;
      Pcnt(Pin *pin, pcnt_unit_handle_t unit, pcnt_channel_handle_t channel)
          : _pin(pin), _unit(unit), _channel(channel) {}
    };

    struct LedcTimer {
      ledc_timer_config_t c;
      int ref;
    };

    LedcTimer *ledcTimers[LEDC_TIMER_MAX] = {};
    std::mutex ledcTimersMutex;
    uint16_t _ledcChannels = 0;

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

    class LEDC : public pin::ILEDC {
     public:
      ~LEDC() override {
        std::lock_guard lock(ledcTimersMutex);
        _ledcChannels &= ~(1 << _channel);
        ledcTimerUnref(_timer);
      }
      esp_err_t config(uint32_t freq_hz, ledc_mode_t mode,
                       ledc_timer_bit_t duty_resolution,
                       ledc_clk_cfg_t clk_cfg) override {
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
        /*LOGI(_pin, "LEDC config: channel=%i, timer=%i, freq=%i,
           duty_res=%i", _channel, _timer, freq_hz, duty_resolution);*/
        return ESP_OK;
      }
      esp_err_t setDuty(uint32_t duty) override {
        ESP_CHECK_RETURN(
            ledc_set_duty(_speedMode, (ledc_channel_t)_channel, duty));
        ESP_CHECK_RETURN(
            ledc_update_duty(_speedMode, (ledc_channel_t)_channel));
        // LOGI(_pin, "LEDC set duty %i", duty);
        return ESP_OK;
      }
      static esp_err_t create(Pin *pin, pin::Feature **feature) {
        std::lock_guard lock(ledcTimersMutex);
        for (auto i = 0; i < LEDC_CHANNEL_MAX; i++)
          if ((_ledcChannels & (1 << i)) == 0) {
            _ledcChannels |= (1 << i);
            *feature = new LEDC(pin, (ledc_channel_t)i);
            return ESP_OK;
          }
        return ESP_FAIL;
      }

     private:
      Pin *_pin;
#if SOC_LEDC_SUPPORT_HS_MODE
      ledc_mode_t _speedMode = LEDC_HIGH_SPEED_MODE;
#else
      ledc_mode_t _speedMode = LEDC_LOW_SPEED_MODE;
#endif
      ledc_channel_t _channel;
      int _timer = -1;

      LEDC(Pin *pin, ledc_channel_t channel) : _pin(pin), _channel(channel) {}
    };

    Pin::Pin(int id) : IPin(id) {
      gpio_num_t num = this->num();
      snprintf(_name, sizeof(_name), "IO%02u", num);
      _features = pin::Flags::None;
      if (GPIO_IS_VALID_DIGITAL_IO_PAD(num))
        _features |= pin::Flags::DigitalInput | pin::Flags::PulseCounter;
      if (GPIO_IS_VALID_OUTPUT_GPIO(num))
        _features |= pin::Flags::DigitalOutput | pin::Flags::PullUp |
                     pin::Flags::PullDown | pin::Flags::LEDC;
#if SOC_DAC_SUPPORTED
      if (num == DAC_CHANNEL_1_GPIO_NUM || num == DAC_CHANNEL_2_GPIO_NUM)
        _features |= pin::Flags::DAC;
#endif
      adc_unit_t unit;
      adc_channel_t channel;
      if (adc_oneshot_io_to_channel(num, &unit, &channel) == ESP_OK)
        _features |= pin::Flags::ADC;
    };
    gpio_num_t Pin::num() {
      return (gpio_num_t)_id;
    }

    void Pin::reset() {
      ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_reset_pin(num()));
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

    esp_err_t Pin::createFeature(pin::Type type, pin::Feature **feature) {
      switch (type) {
        case pin::Type::Digital:
          if ((_features & (pin::Flags::DigitalInput |
                            pin::Flags::DigitalOutput)) != 0) {
            *feature = new gpio::Digital(this);
            return ESP_OK;
          }
          break;
        case pin::Type::ADC:
          if ((_features & pin::Flags::ADC) != 0) {
            gpio::ADC::create(this, feature);
            return ESP_OK;
          }
          break;
#if SOC_DAC_SUPPORTED
        case pin::Type::DAC:
          if ((_features & pin::Flags::DAC) != 0) {
            gpio::DAC::create(this, feature);
            return ESP_OK;
          }
          break;
#endif
        case pin::Type::Pcnt:
          if ((_features & pin::Flags::PulseCounter) != 0) {
            gpio::Pcnt::create(this, feature);
            return ESP_OK;
          }
          break;
        case pin::Type::LEDC:
          if ((_features & pin::Flags::DigitalOutput) != 0) {
            gpio::LEDC::create(this, feature);
            return ESP_OK;
          }
          break;
        default:
          break;
      }
      return IPin::createFeature(type, feature);
    }
  }  // namespace gpio

  namespace io {
    IPin *Gpio::newPin(int id) {
      if (GPIO_IS_VALID_GPIO(id))
        return new gpio::Pin(id);
      return nullptr;
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

    /*adc_oneshot_unit_handle_t adc_handles[] = {nullptr, nullptr};
    std::map<adc_unit_t, std::mutex> _adc_locks;

    esp_err_t getADCUnitHandle(adc_unit_t unit,
                               adc_oneshot_unit_handle_t *handle) {
      auto h = adc_handles[unit];
      if (!h) {
        adc_oneshot_unit_init_cfg_t ucfg = {.unit_id = unit,
                                            .clk_src =
    (adc_oneshot_clk_src_t)0, .ulp_mode = ADC_ULP_MODE_DISABLE};
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
    }*/

  }  // namespace gpio
}  // namespace esp32m