#include "esp32m/debug/gpio.hpp"
#include "esp32m/io/utils.hpp"
#include "esp32m/io/gpio.hpp"
#include "esp32m/json.hpp"

#include <driver/gpio.h>
//#include <soc/adc_channel.h>
#include <esp_adc/adc_oneshot.h>
#include <soc/dac_channel.h>
#include <soc/touch_sensor_channel.h>

namespace esp32m {
  namespace debug {
    const uint64_t gpioMask =
        1 << GPIO_NUM_0    /*!< GPIO0, input and output */
        | 1 << GPIO_NUM_1  /*!< GPIO1, input and output */
        | 1 << GPIO_NUM_2  /*!< GPIO2, input and output */
        | 1 << GPIO_NUM_3  /*!< GPIO3, input and output */
        | 1 << GPIO_NUM_4  /*!< GPIO4, input and output */
        | 1 << GPIO_NUM_5  /*!< GPIO5, input and output */
        | 1 << GPIO_NUM_6  /*!< GPIO6, input and output */
        | 1 << GPIO_NUM_7  /*!< GPIO7, input and output */
        | 1 << GPIO_NUM_8  /*!< GPIO8, input and output */
        | 1 << GPIO_NUM_9  /*!< GPIO9, input and output */
        | 1 << GPIO_NUM_10 /*!< GPIO10, input and output */
        | 1 << GPIO_NUM_11 /*!< GPIO11, input and output */
        | 1 << GPIO_NUM_12 /*!< GPIO12, input and output */
        | 1 << GPIO_NUM_13 /*!< GPIO13, input and output */
        | 1 << GPIO_NUM_14 /*!< GPIO14, input and output */
        | 1 << GPIO_NUM_15 /*!< GPIO15, input and output */
        | 1 << GPIO_NUM_16 /*!< GPIO16, input and output */
        | 1 << GPIO_NUM_17 /*!< GPIO17, input and output */
        | 1 << GPIO_NUM_18 /*!< GPIO18, input and output */
        | 1 << GPIO_NUM_19 /*!< GPIO19, input and output */
        | 1 << GPIO_NUM_20 /*!< GPIO20, input and output */
        | 1 << GPIO_NUM_21 /*!< GPIO21, input and output */
#if CONFIG_IDF_TARGET_ESP32
        | 1 << GPIO_NUM_22 /*!< GPIO22, input and output */
        | 1 << GPIO_NUM_23 /*!< GPIO23, input and output */

        | 1 << GPIO_NUM_25 /*!< GPIO25, input and output */
#endif
        /* Note: The missing IO is because it is used inside the chip. */
        | 1 << GPIO_NUM_26    /*!< GPIO26, input and output */
        | 1 << GPIO_NUM_27    /*!< GPIO27, input and output */
        | 1 << GPIO_NUM_28    /*!< GPIO28, input and output */
        | 1 << GPIO_NUM_29    /*!< GPIO29, input and output */
        | 1 << GPIO_NUM_30    /*!< GPIO30, input and output */
        | 1 << GPIO_NUM_31    /*!< GPIO31, input and output */
        | 1ULL << GPIO_NUM_32 /*!< GPIO32, input and output */
        | 1ULL << GPIO_NUM_33 /*!< GPIO33, input and output */
        | 1ULL << GPIO_NUM_34 /*!< GPIO34, input mode only(ESP32) / input and
                                 output(ESP32-S2) */
        | 1ULL << GPIO_NUM_35 /*!< GPIO35, input mode only(ESP32) / input and
                                 output(ESP32-S2) */
        | 1ULL << GPIO_NUM_36 /*!< GPIO36, input mode only(ESP32) / input and
                                 output(ESP32-S2) */
        | 1ULL << GPIO_NUM_37 /*!< GPIO37, input mode only(ESP32) / input and
                                 output(ESP32-S2) */
        | 1ULL << GPIO_NUM_38 /*!< GPIO38, input mode only(ESP32) / input and
                                 output(ESP32-S2) */
        | 1ULL << GPIO_NUM_39 /*!< GPIO39, input mode only(ESP32) / input and
                                 output(ESP32-S2) */
#if GPIO_PIN_COUNT > 40
        | 1ULL << GPIO_NUM_40 /*!< GPIO40, input and output */
        | 1ULL << GPIO_NUM_41 /*!< GPIO41, input and output */
        | 1ULL << GPIO_NUM_42 /*!< GPIO42, input and output */
        | 1ULL << GPIO_NUM_43 /*!< GPIO43, input and output */
        | 1ULL << GPIO_NUM_44 /*!< GPIO44, input and output */
        | 1ULL << GPIO_NUM_45 /*!< GPIO45, input and output */
        | 1ULL << GPIO_NUM_46 /*!< GPIO46, input mode only */
#endif
        ;

    GPIO &GPIO::instance() {
      static GPIO i;
      return i;
    }

    bool s2pn(const char *s, gpio_num_t &pin) {
      if (!s)
        return false;
      auto l = strlen(s);
      if (l == 0 || l > 2)
        return false;
      char c = s[0];
      if (c < '0' || c > '9')
        return false;
      uint8_t b = c - '0';
      if (l == 2) {
        char c = s[1];
        if (c < '0' || c > '9')
          return false;
        b *= 10;
        b += (c - '0');
      }
      if (b > GPIO_NUM_MAX)
        return false;
      pin = (gpio_num_t)b;
      return true;
    }

    template <typename T>
    bool changeConfigValue(T &target, JsonVariantConst v, bool &changed) {
      if (v.isNull())
        return false;
      T src = v.as<T>();
      if (src == target)
        return false;
      changed = true;
      target = src;
      return true;
    }

    bool changePinMode(gpio_num_t pin, PinConfig &c, PinMode mode) {
      if (c.mode == mode)
        return false;
      memset(&c, 0, sizeof(PinConfig));
      switch (c.mode) {
        case PinMode::Dac: {
         /* dac_channel_t ch;
          if (io::gpio2Dac(pin, ch))
            ESP_ERROR_CHECK_WITHOUT_ABORT(dac_output_disable(ch));*/
        } break;
        default:
          break;
      }
      c.mode = mode;
      ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_reset_pin(pin));
      return true;
    }

    uint16_t GPIO::touchMask() {
      uint16_t result = 0;
      touch_pad_t tp;
      for (auto const &i : _config)
        if (i.second.mode == PinMode::Touch &&
            io::gpio2TouchPad((gpio_num_t)i.first, tp))
          result |= (1 << tp);
      return result;
    }

    bool GPIO::setConfig(const JsonVariantConst cfg,
                         DynamicJsonDocument **result) {
      bool changed = false;
      auto pins = cfg["pins"].as<JsonObjectConst>();
      if (pins) {
        gpio_num_t pin;
        uint64_t pinsInRequest = 0;
        int cw_channel = -1;
        uint8_t led_channels = 0;
        auto touchMask0 = touchMask();
        for (JsonPairConst kv : pins)
          if (s2pn(kv.key().c_str(), pin)) {
            logI("configuring pin %d", pin);
            auto ci = _config.find(pin);
            bool found = ci != _config.end();
            JsonArrayConst a = kv.value().as<JsonArrayConst>();
            esp_err_t err = ESP_FAIL;
            if (a) {
              auto &c = _config[pin];
              pinsInRequest |= (1 << pin);
              PinMode mode = (PinMode)a[0].as<int>();
              changed |= changePinMode(pin, c, mode);
              switch (mode) {
                case PinMode::Digital:
                  err = ESP_OK;
                  if (changeConfigValue(c.digital.mode, a[1], changed))
                    err = ESP_ERROR_CHECK_WITHOUT_ABORT(
                        gpio_set_direction(pin, c.digital.mode));
                  if (!err && changeConfigValue(c.digital.pull, a[2], changed))
                    err = ESP_ERROR_CHECK_WITHOUT_ABORT(
                        gpio_set_pull_mode(pin, c.digital.pull));
                  break;
                case PinMode::Adc: {
                  /*int c1, c2;
                  if (!io::gpio2Adc(pin, c1, c2))
                    break;
                   if (c1 >= 0)
                     err =
                         ESP_ERROR_CHECK_WITHOUT_ABORT(adc1_config_channel_atten(
                             (adc1_channel_t)c1, c.adc.atten));
                   else
                     err =
                         ESP_ERROR_CHECK_WITHOUT_ABORT(adc2_config_channel_atten(
                             (adc2_channel_t)c2, c.adc.atten));*/
                } break;
                case PinMode::Dac: {
                  dac_channel_t ch;
                  if (!io::gpio2Dac(pin, ch))
                    break;
                  err = ESP_OK;
                  /*if (changeConfigValue(c.dac.voltage, a[1], changed))
                    err = ESP_ERROR_CHECK_WITHOUT_ABORT(
                        dac_output_voltage(ch, c.dac.voltage));
                  if (!err)
                    err = ESP_ERROR_CHECK_WITHOUT_ABORT(dac_output_enable(ch));*/
                } break;
                case PinMode::CosineWave: {
                  dac_channel_t ch;
                  if (!io::gpio2Dac(pin, ch))
                    break;
                  if (cw_channel >= 0)  // we can have only one cw
                    break;
                  err = ESP_OK;
                  if (changeConfigValue(c.cw.offset, a[1], changed) |
                      //changeConfigValue(c.cw.scale, a[2], changed) |
                      //changeConfigValue(c.cw.phase, a[3], changed) |
                      changeConfigValue(c.cw.freq, a[4], changed)) {
                    /*dac_cw_config_t cw = {
                        .en_ch = ch,
                        .scale = c.cw.scale,
                        .phase = c.cw.phase,
                        .freq = c.cw.freq,
                        .offset = c.cw.offset,
                    };
                    err = ESP_ERROR_CHECK_WITHOUT_ABORT(
                        dac_cw_generator_config(&cw));
                    if (!err)
                      cw_channel = ch;*/
                  }
                } break;
                case PinMode::Ledc: {
                  bool cc = changeConfigValue(c.ledc.channel, a[1], changed);
                  if (led_channels &
                      (1 << c.ledc.channel))  // only one pin per channel
                    break;
                  err = ESP_OK;
                  if (cc | changeConfigValue(c.ledc.mode, a[2], changed) |
                      changeConfigValue(c.ledc.intr, a[3], changed) |
                      changeConfigValue(c.ledc.timer, a[4], changed) |
                      changeConfigValue(c.ledc.duty, a[5], changed) |
                      changeConfigValue(c.ledc.hpoint, a[6], changed)) {
                    ledc_channel_config_t ledc = {
                        .gpio_num = pin,
                        .speed_mode = c.ledc.mode,
                        .channel = c.ledc.channel,
                        .intr_type = c.ledc.intr,
                        .timer_sel = c.ledc.timer,
                        .duty = c.ledc.duty,
                        .hpoint = c.ledc.hpoint,
                        .flags =
                            {
                                .output_invert = 0,
                            },
                    };
                    err = ESP_ERROR_CHECK_WITHOUT_ABORT(
                        ledc_channel_config(&ledc));
                    if (!err)
                      led_channels |= (1 << c.ledc.channel);
                  }
                } break;
                case PinMode::SigmaDelta:/* {
                  bool cc = changeConfigValue(c.sd.channel, a[1], changed);
                  if (sd_channels &
                      (1 << c.sd.channel))  // only one pin per channel
                    break;
                  err = ESP_OK;
                  if (cc | changeConfigValue(c.sd.duty, a[2], changed) |
                      changeConfigValue(c.sd.prescale, a[3], changed)) {
                    sigmadelta_config_t sd = {
                        .channel = c.sd.channel,
                        .sigmadelta_duty = c.sd.duty,
                        .sigmadelta_prescale = c.sd.prescale,
                        .sigmadelta_gpio = pin,
                    };
                    err = ESP_ERROR_CHECK_WITHOUT_ABORT(sigmadelta_config(&sd));
                    if (!err)
                      sd_channels |= (1 << c.sd.channel);
                  }
                }*/ break;
                case PinMode::PulseCounter: /*{
                  err = ESP_OK;
                  bool restart = false;
                  if (changeConfigValue(c.pc.unit, a[1], changed) |
                      changeConfigValue(c.pc.channel, a[2], changed) |
                      changeConfigValue(c.pc.pos_mode, a[3], changed) |
                      changeConfigValue(c.pc.neg_mode, a[4], changed) |
                      changeConfigValue(c.pc.counter_h_lim, a[5], changed) |
                      changeConfigValue(c.pc.counter_l_lim, a[6], changed)) {
                    pcnt_config_t pcnt = {.pulse_gpio_num = pin,
                                          .ctrl_gpio_num = -1,
                                          .lctrl_mode = PCNT_MODE_KEEP,
                                          .hctrl_mode = PCNT_MODE_KEEP,
                                          .pos_mode = c.pc.pos_mode,
                                          .neg_mode = c.pc.neg_mode,
                                          .counter_h_lim = c.pc.counter_h_lim,
                                          .counter_l_lim = c.pc.counter_l_lim,
                                          .unit = c.pc.unit,
                                          .channel = c.pc.channel};
                    err =
                        ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt_unit_config(&pcnt));
                    restart = true;
                  }
                  if (!err && changeConfigValue(c.pc.filter, a[7], changed))
                    err = ESP_ERROR_CHECK_WITHOUT_ABORT(
                        pcnt_set_filter_value(c.pc.unit, c.pc.filter));
                  if (restart && !err) {
                    ESP_ERROR_CHECK_WITHOUT_ABORT(
                        pcnt_counter_pause(c.pc.unit));
                    ESP_ERROR_CHECK_WITHOUT_ABORT(
                        pcnt_counter_clear(c.pc.unit));
                    ESP_ERROR_CHECK_WITHOUT_ABORT(
                        pcnt_counter_resume(c.pc.unit));
                  }
                }*/ break;
                case PinMode::Touch: {
                  touch_pad_t tp;
                  if (!io::gpio2TouchPad(pin, tp))
                    break;
                  err = ESP_OK;
                  if (!touchMask0) {
                    ESP_ERROR_CHECK_WITHOUT_ABORT(touch_pad_init());
                    ESP_ERROR_CHECK_WITHOUT_ABORT(
                        touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER));
                    ESP_ERROR_CHECK_WITHOUT_ABORT(
                        touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5,
                                              TOUCH_HVOLT_ATTEN_1V));
                    touchMask0 |= (1 << tp);
                  }
                  if (changeConfigValue(c.touch.threshold, a[1], changed))
                    err = ESP_ERROR_CHECK_WITHOUT_ABORT(
                        touch_pad_config(tp, c.touch.threshold));
                  if (!err && (changeConfigValue(c.touch.slope, a[2], changed) |
                               changeConfigValue(c.touch.opt, a[3], changed)))
                    err = ESP_ERROR_CHECK_WITHOUT_ABORT(
                        touch_pad_set_cnt_mode(tp, c.touch.slope, c.touch.opt));
                } break;
                default:
                  break;
              }
            }
            if (err)
              if (found) {
                changed |= changePinMode(pin, _config[pin], PinMode::Undefined);
                _config.erase(ci);
              }
          }
        if (changed) {
          auto touchMask1 = touchMask();
          if (touchMask1 != touchMask0) {
            if (!touchMask1 && touchMask0)
              ESP_ERROR_CHECK_WITHOUT_ABORT(touch_pad_deinit());
          }

          /*if (cw_channel >= 0)
            ESP_ERROR_CHECK_WITHOUT_ABORT(dac_cw_generator_enable());
          else
            ESP_ERROR_CHECK_WITHOUT_ABORT(dac_cw_generator_disable());*/
        }
      }
      auto ledcTimers = cfg["ledcts"].as<JsonArrayConst>();
      if (ledcTimers) {
        for (int i = 0; i < ledcTimers.size(); i++)
          if (i < LEDC_TIMER_MAX) {
            auto timer = ledcTimers[i];
            auto ci = _ledcTimersConfig.find(i);
            bool found = ci != _ledcTimersConfig.end();
            esp_err_t err = ESP_FAIL;
            if (timer) {
              err = ESP_OK;
              if (!found)
                changed = true;
              auto &c = _ledcTimersConfig[i];
              c.timer_num = (ledc_timer_t)i;
              if (changeConfigValue(c.speed_mode, timer[0], changed) |
                  changeConfigValue(c.duty_resolution, timer[1], changed) |
                  changeConfigValue(c.freq_hz, timer[2], changed) |
                  changeConfigValue(c.clk_cfg, timer[3], changed))
                err = ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_timer_config(&c));
            }
            if (err)
              if (found) {
                changed = true;
                _ledcTimersConfig.erase(ci);
              }
          }
      }

      return changed;
    }

    DynamicJsonDocument *GPIO::getConfig(const JsonVariantConst args) {
      char numbuf[4];
      size_t capa = JSON_OBJECT_SIZE(3);  // root+pins+ledc-timer
      for (auto const &i : _config) {
        size_t arr_len = 1;  // PinMode
        switch (i.second.mode) {
          case PinMode::Digital:
            arr_len += 2;
            break;
          case PinMode::Adc:
            arr_len += 1;
            break;
          case PinMode::Dac:
            arr_len += 1;
            break;
          case PinMode::CosineWave:
            arr_len += 4;
            break;
          case PinMode::Ledc:
            arr_len += 6;
            break;
          case PinMode::SigmaDelta:
            arr_len += 2;
            break;
          case PinMode::PulseCounter:
            arr_len += 7;
            break;
          case PinMode::Touch:
            arr_len += 3;
            break;
          default:
            break;
        }
        capa += JSON_OBJECT_SIZE(1) + 4  // numeric index as string
                + JSON_ARRAY_SIZE(arr_len);
      }
      capa += JSON_ARRAY_SIZE(LEDC_TIMER_MAX) +
              _ledcTimersConfig.size() * JSON_ARRAY_SIZE(4);
      auto doc = new DynamicJsonDocument(capa);
      auto root = doc->to<JsonObject>();
      auto pins = root.createNestedObject("pins");
      for (auto const &i : _config) {
        sprintf(numbuf, "%d", i.first);
        auto c = pins.createNestedArray(numbuf);
        c.add(i.second.mode);
        switch (i.second.mode) {
          case PinMode::Digital:
            c.add(i.second.digital.mode);
            c.add(i.second.digital.pull);
            break;
          case PinMode::Adc:
            //c.add(i.second.adc.atten);
            break;
          case PinMode::Dac:
            c.add(i.second.dac.voltage);
            break;
          case PinMode::CosineWave:
            c.add(i.second.cw.offset);
            //c.add(i.second.cw.scale);
            //c.add(i.second.cw.phase);
            c.add(i.second.cw.freq);
            break;
          case PinMode::Ledc:
            c.add(i.second.ledc.channel);
            c.add(i.second.ledc.mode);
            c.add(i.second.ledc.intr);
            c.add(i.second.ledc.timer);
            c.add(i.second.ledc.duty);
            c.add(i.second.ledc.hpoint);
            break;
          case PinMode::SigmaDelta:
            c.add(i.second.sd.rate);
            c.add(i.second.sd.duty);
            break;
          case PinMode::PulseCounter:
           /* c.add(i.second.pc.unit);
            c.add(i.second.pc.channel);
            c.add(i.second.pc.pos_mode);
            c.add(i.second.pc.neg_mode);
            c.add(i.second.pc.counter_h_lim);
            c.add(i.second.pc.counter_l_lim);
            c.add(i.second.pc.filter);*/
            break;
          case PinMode::Touch:
            c.add(i.second.touch.threshold);
            c.add(i.second.touch.slope);
            c.add(i.second.touch.opt);
            break;
          default:
            break;
        }
      }
      auto ledcTimers = root.createNestedArray("ledcts");
      for (int i = 0; i < LEDC_TIMER_MAX; i++) {
        auto ci = _ledcTimersConfig.find(i);
        if (ci == _ledcTimersConfig.end())
          ledcTimers.add(nullptr);
        else {
          auto a = ledcTimers.createNestedArray();
          auto &c = ci->second;
          a.add(c.speed_mode);
          a.add(c.duty_resolution);
          a.add(c.freq_hz);
          a.add(c.clk_cfg);
        }
      }
      return doc;
    }

    void GPIO::setState(const JsonVariantConst state,
                        DynamicJsonDocument **result) {
      esp_err_t err = ESP_OK;
      auto pins = state["pins"].as<JsonObjectConst>();
      if (pins) {
        gpio_num_t pin;
        for (JsonPairConst kv : pins)
          if (s2pn(kv.key().c_str(), pin)) {
            logI("setting state of pin %d", pin);
            auto ci = _config.find(pin);
            bool found = ci != _config.end();
            if (found)
              switch (ci->second.mode) {
                case PinMode::Digital:
                  err = gpio_set_level(pin, kv.value().as<int>());
                  break;
                default:
                  break;
              }
            if (err != ESP_OK)
              break;
          }
      }
      json::checkSetResult(err, result);
    }

    DynamicJsonDocument *GPIO::getState(const JsonVariantConst args) {
      size_t capa = JSON_OBJECT_SIZE(2);  // root+pins
      for (auto const &i : _config) {
        capa += JSON_OBJECT_SIZE(1) + 4  // numeric index as string
            ;

        switch (i.second.mode) {
          case PinMode::Adc:
            capa += JSON_ARRAY_SIZE(2);
            break;
          default:
            break;
        }
      }
      DynamicJsonDocument *doc = new DynamicJsonDocument(capa);
      auto root = doc->to<JsonObject>();
      auto pins = root.createNestedObject("pins");
      char numbuf[4];
      for (auto const &i : _config) {
        sprintf(numbuf, "%d", i.first);
        gpio_num_t pin = (gpio_num_t)i.first;
        switch (i.second.mode) {
          case PinMode::Digital:
            pins[numbuf] = gpio_get_level(pin);
            break;
          case PinMode::Adc: {
            int raw;
            adc_unit_t unit;
            adc_channel_t channel;
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                adc_oneshot_io_to_channel(pin, &unit, &channel));
            auto c = pins.createNestedArray(numbuf);
            adc_oneshot_unit_handle_t adchandle;
            if (ESP_ERROR_CHECK_WITHOUT_ABORT(
                    gpio::getADCUnitHandle(unit, &adchandle)) == 0) {
              /*adc_oneshot_chan_cfg_t ccfg = {
                  .atten = i.second.adc.atten,  // ADC_ATTEN_DB_11,
                  .bitwidth = ADC_BITWIDTH_DEFAULT,
              };
              ESP_ERROR_CHECK_WITHOUT_ABORT(
                  adc_oneshot_config_channel(adchandle, channel, &ccfg));*/
              ESP_ERROR_CHECK_WITHOUT_ABORT(
                  adc_oneshot_read(adchandle, channel, &raw));
            } else
              /*if (c1 >= 0)
                c.add(adc1_get_raw((adc1_channel_t)c1));
              else if (!ESP_ERROR_CHECK_WITHOUT_ABORT(adc2_get_raw(
                           (adc2_channel_t)c2, ADC_BITWIDTH_12, &raw)))
                c.add(raw);
              else*/
              c.add(nullptr);
          } break;
          case PinMode::PulseCounter:/* {
            int16_t count;
            if (!ESP_ERROR_CHECK_WITHOUT_ABORT(
                    pcnt_get_counter_value(i.second.pc.unit, &count)))
              pins[numbuf] = count;
          }*/ break;
          case PinMode::Touch: {
            touch_pad_t tp;
            if (!io::gpio2TouchPad(pin, tp))
              break;
            uint16_t value;
            if (!ESP_ERROR_CHECK_WITHOUT_ABORT(touch_pad_read(tp, &value)))
              pins[numbuf] = value;
          } break;
          default:
            break;
        }
      };
      return doc;
    }

    GPIO *useGPIO() {
      return &GPIO::instance();
    }
  }  // namespace debug
}  // namespace esp32m