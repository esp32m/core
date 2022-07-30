#include <driver/gpio.h>

#include "esp32m/app.hpp"
#include "esp32m/config/changed.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/dev/hbridge.hpp"

namespace esp32m {
  namespace dev {
    esp_err_t HBridge::init() {
      ESP_CHECK_RETURN(_pinFwd->setDirection(GPIO_MODE_INPUT_OUTPUT));
      ESP_CHECK_RETURN(_pinRev->setDirection(GPIO_MODE_INPUT_OUTPUT));
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
        case Mode::Break:
          r = false;
          f = false;
          break;
        default:
          return ESP_FAIL;
      }
      ESP_CHECK_RETURN(_pinFwd->digitalWrite(f));
      ESP_CHECK_RETURN(_pinRev->digitalWrite(r));
      ESP_CHECK_RETURN(refresh());
      if (_persistent)
        EventConfigChanged::publish(this);
      return ESP_OK;
    }

    esp_err_t HBridge::refresh() {
      bool f, r;
      ESP_CHECK_RETURN(_pinFwd->digitalRead(f));
      ESP_CHECK_RETURN(_pinRev->digitalRead(r));
      if (f && r)
        _mode = Mode::Off;
      else if (!f && r)
        _mode = Mode::Forward;
      else if (f && !r)
        _mode = Mode::Reverse;
      else
        _mode = Mode::Break;
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

    DynamicJsonDocument *HBridge::getConfig(const JsonVariantConst args) {
      if (_persistent)
        return getState(args);
      return nullptr;
    }

    HBridge *useHBridge(const char *name, io::IPin *pinFwd, io::IPin *pinRev) {
      return new HBridge(name, pinFwd, pinRev);
    }
  }  // namespace dev
}  // namespace esp32m