#pragma once

#include "esp32m/app.hpp"

#include <map>

//#include <driver/adc.h>
//#include <driver/dac.h>
#include <driver/ledc.h>
//#include <driver/pcnt.h>
#include <driver/pulse_cnt.h>
#include <driver/sdm.h>
#include <driver/touch_sensor.h>

namespace esp32m {

  namespace debug {
    enum PinMode {
      Undefined,
      Digital,
      Adc,
      Dac,
      CosineWave,
      Ledc,
      SigmaDelta,
      PulseCounter,
      Touch
    };

    struct PinConfig {
      PinMode mode;
      union {
        struct {
          gpio_mode_t mode;
          gpio_pull_mode_t pull;
        } digital;
        struct {
          //adc_atten_t atten;
        } adc;
        struct {
          uint8_t voltage;
        } dac;
        struct {
          int8_t offset;
          //dac_cw_scale_t scale;
          //dac_cw_phase_t phase;
          uint32_t freq;
        } cw;
        struct {
          ledc_channel_t channel;
          ledc_mode_t mode;
          ledc_intr_type_t intr;
          ledc_timer_t timer;
          uint32_t duty;
          int hpoint;
        } ledc;
        struct {
          uint32_t rate;
          int8_t duty;
        } sd;
        struct {
         /* pcnt_unit_t unit;
          pcnt_channel_t channel;
          pcnt_count_mode_t pos_mode;
          pcnt_count_mode_t neg_mode;
          int16_t counter_h_lim;
          int16_t counter_l_lim;
          uint16_t filter;*/
        } pc;
        struct {
          uint16_t threshold;
          touch_cnt_slope_t slope;
          touch_tie_opt_t opt;
        } touch;
      };
    };

    class GPIO : public AppObject {
     public:
      GPIO(const GPIO &) = delete;
      static GPIO &instance();
      const char *name() const override {
        return "gpio";
      }

     protected:
      void setState(const JsonVariantConst state,
                    DynamicJsonDocument **result) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;

     private:
      std::map<uint8_t, PinConfig> _config;
      std::map<uint8_t, ledc_timer_config_t> _ledcTimersConfig;
      uint16_t touchMask();
      GPIO() {}
    };

    GPIO *useGPIO();

  }  // namespace debug

}  // namespace esp32m