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
        case Mode::Brake:
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
        case Mode::Off:
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
      if (!isPwm()) {
        bool f, r;
        ESP_CHECK_RETURN(getPins(f, r));
        _mode = f ? (r ? Mode::Brake : Mode::Reverse)
                  : (r ? Mode::Forward : Mode::Off);
      }
      return ESP_OK;
    }

    esp_err_t HBridge::setPins(bool fwd, bool rev) {
      // logD("setPins %d %d PWM=%d", fwd, rev, isPwm());
      if (isPwm()) {
        if (fwd && rev) {
          ESP_CHECK_RETURN(_fwd->pwm()->enable(false));
          ESP_CHECK_RETURN(_rev->pwm()->enable(false));
        } else {
          ESP_CHECK_RETURN(_fwd->pwm()->enable(fwd));
          ESP_CHECK_RETURN(_rev->pwm()->enable(rev));
        }
      }
      ESP_CHECK_RETURN(_fwd->digital()->write(fwd));
      ESP_CHECK_RETURN(_rev->digital()->write(rev));
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

    JsonDocument *HBridge::getState(RequestContext &ctx) {
      auto doc = new JsonDocument(); /* JSON_OBJECT_SIZE(2) */
      JsonObject info = doc->to<JsonObject>();
      info["mode"] = _mode;
      float current;
      if (_cs && _cs->read(current) == ESP_OK)
        info["current"] = current;
      return doc;
    }

    void HBridge::setState(RequestContext &ctx) {
      bool changed = false;
      auto mode = _mode;
      json::from(ctx.data["mode"], mode, &changed);
      if (changed)
        ctx.errors.check(run(mode, _duty));
    }

    bool HBridge::setConfig(RequestContext &ctx) {
      bool changed = false;
      auto mode = _mode;
      if (_persistent)
        json::from(ctx.data["mode"], mode, &changed);
      json::from(ctx.data["speed"], _duty, &changed);
      if (changed)
        ctx.errors.check(run(mode, _duty));
      return changed;
    }

    JsonDocument *HBridge::getConfig(RequestContext &ctx) {
      auto doc = new JsonDocument(); /* JSON_OBJECT_SIZE(2) */
      JsonObject info = doc->to<JsonObject>();
      if (_persistent)
        info["mode"] = _mode;
      info["speed"] = _duty;
      return doc;
    }

    HBridge *useHBridge(const char *name, io::IPin *pinFwd, io::IPin *pinRev) {
      return new HBridge(name, pinFwd, pinRev);
    }
  }  // namespace dev
}  // namespace esp32m