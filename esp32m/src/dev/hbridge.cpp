#include <driver/gpio.h>

#include "esp32m/app.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/dev/hbridge.hpp"

namespace esp32m {
  namespace dev {
    esp_err_t HBridge::init() {
      if (!isPwm()) {
        ESP_CHECK_RETURN(_fwd->pwm()->enable(false));
        ESP_CHECK_RETURN(_rev->pwm()->enable(false));
        ESP_CHECK_RETURN(_fwd->digital()->setDirection(true, true));
        ESP_CHECK_RETURN(_rev->digital()->setDirection(true, true));
      }
      return refresh();
    }

    esp_err_t HBridge::run(Mode mode, float speed) {
      bool f, r;
      switch (mode) {
        case Mode::Off:
          r = true;
          f = true;
          break;
        case Mode::Forward:
          r = true;
          f = false;
          break;
        case Mode::Reverse:
          r = false;
          f = true;
          break;
        case Mode::Brake:
          r = false;
          f = false;
          break;
        default:
          return ESP_FAIL;
      }
      ESP_CHECK_RETURN(setSpeed(speed));
      // logD("run %d", mode);
      ESP_CHECK_RETURN(setPins(f, r));
      ESP_CHECK_RETURN(refresh());
      if (_persistent)
        config::Changed::publish(this);
      return ESP_OK;
    }

    esp_err_t HBridge::refresh() {
      bool f, r;
      ESP_CHECK_RETURN(getPins(f, r));
      if (f && r)
        _mode = Mode::Off;
      else if (!f && r)
        _mode = Mode::Forward;
      else if (f && !r)
        _mode = Mode::Reverse;
      else
        _mode = Mode::Brake;
      return ESP_OK;
    }

    esp_err_t HBridge::setPins(bool fwd, bool rev) {
      if (isPwm()) {
        ESP_CHECK_RETURN(_fwd->pwm()->enable(fwd));
        ESP_CHECK_RETURN(_rev->pwm()->enable(rev));
      } else {
        ESP_CHECK_RETURN(_fwd->digital()->write(fwd));
        ESP_CHECK_RETURN(_rev->digital()->write(rev));
      }
      return ESP_OK;
    }

    esp_err_t HBridge::getPins(bool &fwd, bool &rev) {
      if (isPwm()) {
        fwd = _fwd->pwm()->isEnabled();
        rev = _rev->pwm()->isEnabled();
      } else {
        ESP_CHECK_RETURN(_fwd->digital()->read(fwd));
        ESP_CHECK_RETURN(_rev->digital()->read(rev));
      }
      return ESP_OK;
    }

    DynamicJsonDocument *HBridge::getState(const JsonVariantConst args) {
      auto doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(2));
      JsonObject info = doc->to<JsonObject>();
      info["mode"] = _mode;
      info["speed"] = _speed;
      return doc;
    }

    void HBridge::setState(const JsonVariantConst state,
                           DynamicJsonDocument **result) {
      json::from(state["mode"], _mode);
      json::from(state["speed"], _speed);
      json::checkSetResult(run(_mode, _speed), result);
    }

    bool HBridge::setConfig(const JsonVariantConst cfg,
                            DynamicJsonDocument **result) {
      if (_persistent) {
        setState(cfg, result);
        return true;
      }
      return false;
    }

    DynamicJsonDocument *HBridge::getConfig(RequestContext &ctx) {
      if (_persistent)
        return getState(ctx.request.data());
      return nullptr;
    }

    HBridge *useHBridge(const char *name, io::IPin *pinFwd, io::IPin *pinRev) {
      return new HBridge(name, pinFwd, pinRev);
    }
  }  // namespace dev
}  // namespace esp32m