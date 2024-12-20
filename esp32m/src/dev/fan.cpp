#include "esp32m/dev/fan.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

#include <math.h>
#include <limits>

namespace esp32m {
  namespace dev {

    Fan::Fan(const char *name, io::IPin *power, io::IPin *tach)
        : _name(name), _power(power), _tach(tach) {
      Device::init(Flags::HasSensors);
      if (_tach && _tach->pcnt()) {
        _sensorRpm = new Sensor(this, "measurement");
        _sensorRpm->unit = "RPM";
      }
    }
    Fan::~Fan() {
      if (_sensorRpm)
        delete _sensorRpm;
    }

    bool Fan::initSensors() {
      if (!_tach)
        return false;
      auto pcnt = _tach->pcnt();
      if (!pcnt)
        return false;
      ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt->setEdgeAction(
          PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
      ESP_ERROR_CHECK_WITHOUT_ABORT(
          pcnt->setFilter(12 * 1000));  // 12 us is as high as it can go
      if (!pcnt->isEnabled())
        ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt->enable(true));
      return pcnt->isEnabled();
    }

    DynamicJsonDocument *Fan::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(4));
      JsonObject obj = doc->to<JsonObject>();
      json::to(obj, "stamp", millis() - _stamp);
      if (_tach)
        json::to(obj, "rpm", _rpm);
      json::to(obj, "on", _on);
      if (_pwm) {
        json::to(obj, "speed", _speed);
      }
      return doc;
    }

    void Fan::setState(const JsonVariantConst cfg,
                       DynamicJsonDocument **result) {
      bool changed = false;
      auto speed = _speed;
      if (json::from(cfg["speed"], speed, &changed))
        setSpeed(speed);

      if (cfg["on"].is<bool>()) {
        bool on = _on;
        json::from(cfg["on"], on, &changed);
        turn(on);
      }
    }

    bool Fan::pollSensors() {
      auto pcnt = _tach ? _tach->pcnt() : nullptr;
      if (!pcnt)
        return false;
      int pc;
      auto err = ESP_ERROR_CHECK_WITHOUT_ABORT(pcnt->read(pc, true));
      if (err == ESP_OK) {
        auto ms = millis();
        auto passed = ms - _stamp;
        _rpm = pc * 60000.0f / passed / _ppr;
        _stamp = ms;
        _sensorRpm->set(_rpm);
      }
      return true;
    }

    bool Fan::setConfig(const JsonVariantConst cfg,
                        DynamicJsonDocument **result) {
      bool changed = false;
      json::from(cfg["on"], _on, &changed);
      json::from(cfg["speed"], _speed, &changed);
      if (changed)
        applyConfig();
      return changed;
    }

    DynamicJsonDocument *Fan::getConfig(RequestContext &ctx) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(2));
      JsonObject root = doc->to<JsonObject>();
      json::to(root, "on", _on);
      if (_pwm) {
        json::to(root, "speed", _speed);
      }
      return doc;
    }
    esp_err_t Fan::configurePwm(io::IPin *pin, uint32_t freq) {
      io::pin::IPWM *pwm;
      if (_pwm != pin) {
        auto old = _pwm ? _pwm->pwm() : nullptr;
        if (old)
          old->enable(false);
        _pwm = pin;
      }
      _freq = freq;
      if (!pin)
        return ESP_OK;
      pwm = _pwm ? _pwm->pwm() : nullptr;
      if (!pwm)
        return ESP_FAIL;
      ESP_CHECK_RETURN(pwm->setFreq(_freq));
      return ESP_OK;
    }
    esp_err_t Fan::applyConfig() {
      auto power = _power ? _power->digital() : nullptr;
      auto pwm = _pwm ? _pwm->pwm() : nullptr;
      if (!power && !pwm)
        return ESP_FAIL;
      if (pwm) {
        ESP_CHECK_RETURN(pwm->setDuty(_speed));
        ESP_CHECK_RETURN(pwm->enable(_on && _speed > 0 && _speed <= 1));
      }
      if (power)
        ESP_CHECK_RETURN(power->write(_on));
      return ESP_OK;
    }
    esp_err_t Fan::update() {
      ESP_CHECK_RETURN(applyConfig());
      config::Changed::publish(this);
      return ESP_OK;
    }
    esp_err_t Fan::setSpeed(float speed) {
      _speed = speed;
      return update();
    }
    esp_err_t Fan::turn(bool on) {
      if (on == _on)
        return ESP_OK;
      _on = on;
      return update();
    }

    Fan *useFan(const char *name, io::IPin *power, io::IPin *tach) {
      return new Fan(name, power, tach);
    }
  }  // namespace dev
}  // namespace esp32m