#include "esp32m/dev/buzzer.hpp"

#include <driver/ledc.h>
#include <esp_task_wdt.h>

namespace esp32m {

  namespace dev {

    Buzzer::Buzzer(gpio_num_t pin) : _pin(pin) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_reset_pin(_pin));
      gpio_set_level(_pin, 0);
      ledc_channel_config_t ledc_conf = {
          .gpio_num = _pin,
          .speed_mode = LEDC_HIGH_SPEED_MODE,
          .channel = LEDC_CHANNEL_0,
          .intr_type = LEDC_INTR_DISABLE,
          .timer_sel = LEDC_TIMER_0,
          .duty = 0x0,
          .hpoint = 0,
          .flags =
              {
                  .output_invert = 0,
              },
      };
      ledc_channel_config(&ledc_conf);
    }

    void Buzzer::play(uint32_t freq, uint32_t duration) {
      ledc_timer_config_t timer_conf = {
          .speed_mode = LEDC_HIGH_SPEED_MODE,
          .duty_resolution = LEDC_TIMER_10_BIT,
          .timer_num = LEDC_TIMER_0,
          .freq_hz = freq,
          .clk_cfg = LEDC_AUTO_CLK,
      };
      ledc_timer_config(&timer_conf);

      if (freq) {
        ledc_set_duty(timer_conf.speed_mode, LEDC_CHANNEL_0, 0x7F);
        ledc_update_duty(timer_conf.speed_mode, LEDC_CHANNEL_0);
      }
      vTaskDelay(duration / portTICK_PERIOD_MS);
      ledc_set_duty(timer_conf.speed_mode, LEDC_CHANNEL_0, 0);
      ledc_update_duty(timer_conf.speed_mode, LEDC_CHANNEL_0);
      esp_task_wdt_reset();
      gpio_set_level(_pin, 0);
    }  // namespace esp32m

    bool Buzzer::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is(name(), "play")) {
        JsonArrayConst data = req.data();
        if (data) {
          int i = 0;
          while (i < data.size()) {
            auto v = data[i++];
            if (v.is<JsonArrayConst>())
              play(v[0], v[1]);
            else if (v.is<int>())
              play(v, data[i++]);
          }
        }
        req.respond();
        return true;
      }
      return false;
    }

    void useBuzzer(gpio_num_t pin) {
      new Buzzer(pin);
    }

  }  // namespace dev
}  // namespace esp32m