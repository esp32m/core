#pragma once

#include <functional>
#include "esp_err.h"
#include "esp_timer.h"

namespace esp32m {
  namespace io {

    namespace pwm {
      typedef std::function<void(bool)> Callback;
    }

    class SoftPwm {
     public:
      SoftPwm(pwm::Callback cb) : _cb(cb) {}
      esp_err_t setDuty(float value) {
        _duty = value;
        return ESP_OK;
      }
      float getDuty() const {
        return _duty;
      }
      esp_err_t setFreq(uint32_t value) {
        _freq = value;
        return ESP_OK;
      }
      uint32_t getFreq() const {
        return _freq;
      }
      esp_err_t enable(bool on) {
        if (on != _enabled) {
          if (on) {
            ESP_CHECK_RETURN(ensureCreated());
            _enabled = true;
            SoftPwm::timerCb(this);
          } else {
            _enabled = false;
            if (_timer)
              ESP_CHECK_RETURN(esp_timer_stop(_timer));
          }
        }
        return ESP_OK;
      }
      bool isEnabled() const {
        return _enabled;
      }

     private:
      pwm::Callback _cb;
      float _duty = 0.5;
      uint32_t _freq = 50;
      bool _high = false;
      bool _enabled = false;
      esp_timer_handle_t _timer = nullptr;
      esp_err_t ensureCreated() {
        esp_err_t err = ESP_OK;
        if (_timer == nullptr) {
          esp_timer_create_args_t args = {.callback = SoftPwm::timerCb,
                                          .arg = this,
                                          .dispatch_method = ESP_TIMER_TASK,
                                          .name = "PWM",
                                          .skip_unhandled_events = false};
          err = esp_timer_create(&args, &_timer);
        }
        return err;
      }
      static void timerCb(void* arg) {
        auto self = (SoftPwm*)arg;
        self->onTimer();
      }
      void onTimer() {
        const uint32_t ticksInHz = 1000000;  // microseconds resolution
        if (!_enabled)
          return;
        _high = !_high;
        uint32_t period = ticksInHz / _freq;
        uint32_t thisHalf = _high ? period * _duty : period * (1 - _duty);
        esp_timer_start_once(_timer, thisHalf);
        _cb(_high);
      }
    };
  }  // namespace io
}  // namespace esp32m