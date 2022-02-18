#include "esp32m/dev/servo.hpp"

namespace esp32m {
  namespace dev {
    namespace servo {
      const int MinPulseWidth = 500;
      const int MaxPulseWidth = 2500;
    }  // namespace servo
    Servo::Servo(const char *name, io::IPin *pin) : _name(name), _pin(pin) {
      if (pin) {
        pin->reset();
        pin->digitalWrite(false);
        _ledc = pin->ledc();
      } else
        _ledc = nullptr;
      _configDirty = true;
    }
    esp_err_t Servo::config(int pwMin, int pwMax, int freq,
                            ledc_timer_bit_t timerRes) {
      if (pwMin < servo::MinPulseWidth)
        pwMin = servo::MinPulseWidth;
      _pwMin = pwMin;
      if (pwMax > servo::MaxPulseWidth)
        pwMax = servo::MaxPulseWidth;
      _pwMax = pwMax;
      _freq = freq;
      if (timerRes < LEDC_TIMER_1_BIT)
        timerRes = LEDC_TIMER_1_BIT;
      if (timerRes >= LEDC_TIMER_BIT_MAX)
        timerRes = (ledc_timer_bit_t)(LEDC_TIMER_BIT_MAX - 1);
      _timerRes = timerRes;
      _configDirty = true;
      return ESP_OK;
    }
    esp_err_t Servo::setAngle(float value) {
      if (value < 0)
        value = 0;
      else if (value > 180)
        value = 180;
      auto ms = map(value, 0, 180, _pwMin, _pwMax);
      return setPulseWidth(ms);
    }
    float Servo::getAngle() {
      return map(getPulseWidth() + 1, _pwMin, _pwMax, 0, 180);
    }
    esp_err_t Servo::setPulseWidth(int value) {
      if (!_ledc)
        return ESP_FAIL;
      if (_configDirty) {
        ESP_CHECK_RETURN(_ledc->config(_freq, LEDC_HIGH_SPEED_MODE, _timerRes));
        _configDirty = false;
      }
      if (value < _pwMin)
        value = _pwMin;
      else if (value > _pwMax)
        value = _pwMax;
      value = usToTicks(value);
      ESP_CHECK_RETURN(_ledc->setDuty(value));
      _pulseWidth = value;
      return ESP_OK;
    }

    int Servo::getPulseWidth() {
      return ticksToUs(_pulseWidth);
    }

    void Servo::setState(const JsonVariantConst state,
                         DynamicJsonDocument **result) {
      JsonVariantConst v = state["angle"];
      if (!v.isNull() && v.is<float>()) {
        json::checkSetResult(setAngle(v.as<float>()), result);
        return;
      }
      v = state["pw"];
      int w = -1;
      if (!v.isNull() && v.is<int>())
        w = v.as<int>();
      if (w == -1)
        w = getPulseWidth();
      json::checkSetResult(setPulseWidth(w), result);
    }

    DynamicJsonDocument *Servo::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(2));
      JsonObject info = doc->to<JsonObject>();
      info["angle"] = getAngle();
      info["pw"] = getPulseWidth();
      return doc;
    }

    bool Servo::setConfig(const JsonVariantConst cfg,
                          DynamicJsonDocument **result) {
      if (isPersistent()) {
        setState(cfg, result);
        return true;
      }
      return false;
    }

    DynamicJsonDocument *Servo::getConfig(const JsonVariantConst args) {
      if (isPersistent())
        return getState(args);
      return nullptr;
    }

    int Servo::usToTicks(int usec) {
      return (int)((uint64_t)usec * _freq * (1 << _timerRes) / 1000000);
      // return (int)((float)usec / ((float)_freq / (1 << _timerRes)));
    }

    int Servo::ticksToUs(int duty) {
      return (int) ((uint64_t)duty*1000000/((uint64_t)_freq*(1 << _timerRes)));
      //return (int)((float)ticks * ((float)_freq / (1 << _timerRes)));
    }

    Servo *useServo(const char *name, io::IPin *pin) {
      return new Servo(name, pin);
    }

  }  // namespace dev
}  // namespace esp32m
