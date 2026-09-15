#include <driver/gpio.h>

#include "esp32m/app.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/io/pins.hpp"
#include "esp32m/dev/hbridge.hpp"

namespace esp32m {
  namespace dev {
    esp_err_t HBridge::init() {
      std::lock_guard guard(io::pin::Tx::mutex());
      _initialized = false;
      if (!_fwd || !_rev || !_fwd->digital() || !_rev->digital())
        return ESP_ERR_INVALID_ARG;
      // Expander output latches survive an ESP32-only reset. Establish Off
      // before changing either pin direction, and never adopt old motor state.
      ESP_CHECK_RETURN(setPins(false, false));
      _mode = Off;
      ESP_CHECK_RETURN(_fwd->digital()->setDirection(true, true));
      ESP_CHECK_RETURN(_rev->digital()->setDirection(true, true));
      _initialized = true;
      return ESP_OK;
    }

    esp_err_t HBridge::run(Mode mode, float speed) {
      std::lock_guard guard(io::pin::Tx::mutex());
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
      if (!_initialized)
        ESP_CHECK_RETURN(init());
      ESP_CHECK_RETURN(setSpeed(speed));
      // logD("run %d", mode);
      auto err = setPins(f, r);
      if (err) {
        _initialized = false;
        return err;
      }
      ESP_CHECK_RETURN(refresh());
      if (_persistent)
        config::Changed::publish(this);
      return ESP_OK;
    }

    esp_err_t HBridge::refresh() {
      std::lock_guard guard(io::pin::Tx::mutex());
      if (!isPwm()) {
        bool f, r;
        ESP_CHECK_RETURN(getPins(f, r));
        _mode = f ? (r ? Mode::Brake : Mode::Reverse)
                  : (r ? Mode::Forward : Mode::Off);
      }
      return ESP_OK;
    }

    esp_err_t HBridge::setPins(bool fwd, bool rev) {
      std::lock_guard guard(io::pin::Tx::mutex());
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
      io::pin::Tx tx(io::pin::Tx::Type::Write);
      auto err = _fwd->digital()->write(fwd);
      if (!err)
        err = _rev->digital()->write(rev);
      if (err) {
        tx.abort();
        return err;
      }
      return tx.commit();
    }

    esp_err_t HBridge::getPins(bool &fwd, bool &rev) {
      std::lock_guard guard(io::pin::Tx::mutex());
      if (isPwm()) {
        fwd = _fwd->pwm()->isEnabled();
        rev = _rev->pwm()->isEnabled();
      } else {
        io::pin::Tx tx(io::pin::Tx::Type::Read);
        ESP_CHECK_RETURN(_fwd->digital()->read(fwd));
        ESP_CHECK_RETURN(_rev->digital()->read(rev));
      }
      return ESP_OK;
    }

    JsonDocument *HBridge::getState(RequestContext &ctx) {
      std::lock_guard guard(io::pin::Tx::mutex());
      auto doc = new JsonDocument(); /* JSON_OBJECT_SIZE(2) */
      JsonObject info = doc->to<JsonObject>();
      info["mode"] = _mode;
      float current;
      if (_cs && _cs->read(current) == ESP_OK)
        info["current"] = current;
      return doc;
    }

    void HBridge::setState(RequestContext &ctx) {
      std::lock_guard guard(io::pin::Tx::mutex());
      bool changed = false;
      auto mode = _mode;
      json::from(ctx.data["mode"], mode, &changed);
      if (changed)
        ctx.errors.check(run(mode, _duty));
    }

    bool HBridge::setConfig(RequestContext &ctx) {
      std::lock_guard guard(io::pin::Tx::mutex());
      bool changed = false;
      auto mode = _mode;
      auto duty = _duty;
      if (_persistent)
        json::from(ctx.data["mode"], mode, &changed);
      json::from(ctx.data["speed"], duty, &changed);
      if (changed)
        ctx.errors.check(run(mode, duty));
      return changed;
    }

    JsonDocument *HBridge::getConfig(RequestContext &ctx) {
      std::lock_guard guard(io::pin::Tx::mutex());
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
