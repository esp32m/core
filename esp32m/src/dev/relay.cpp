#include <driver/gpio.h>

#include "esp32m/app.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/dev/relay.hpp"
#include "esp32m/ha/ha.hpp"

namespace esp32m {
  namespace dev {
    void Relay::init() {
      Device::init(Flags::AcceptsCommands);
      ESP_ERROR_CHECK_WITHOUT_ABORT(_pinOn->setDirection(true, true));
      setOnLevel(Pin::On, true);
      if (_pinOff != _pinOn) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(_pinOff->setDirection(true, true));
        setOnLevel(Pin::Off, true);
      }
      if (_pinSenseOn) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(_pinSenseOn->setDirection(true, false));
        setOnLevel(Pin::SenseOn, false);
        if (_pinSenseOff) {
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              _pinSenseOff->setDirection(true, false));
          setOnLevel(Pin::SenseOff, false);
        }
        if (!isPersistent())
          refreshState();
      }
    }

    void Relay::setOnLevel(Pin pin, bool level) {
      uint8_t bit = 1 << (int)pin;
      if (level)
        _levels |= bit;
      else
        _levels &= ~bit;
      switch (pin) {
        case Pin::SenseOn:
          if (_pinSenseOn)
            _pinSenseOn->setPull(!level, level);
          break;
        case Pin::SenseOff:
          if (_pinSenseOff)
            _pinSenseOff->setPull(!level, level);
          break;

        default:
          break;
      }
    }

    bool Relay::getLevel(Pin pin, bool on) {
      return !(on ^ ((_levels & (1 << (int)pin)) != 0));
    }

    void Relay::setState(State state) {
      if (state == _state)
        return;
      logI("state changed: %s -> %s", toString(_state), toString(state));
      _state = state;
      EventStateChanged::publish(this);
      if (isPersistent())
        config::Changed::publish(this);
    }

    esp_err_t Relay::turn(bool on) {
      auto noSense = !_pinSenseOn && !_pinSenseOff;
      auto onLevel = getLevel(Pin::On, on);
      auto offLevel = getLevel(Pin::Off, on);
      if (_pinOff == _pinOn)
        ESP_CHECK_RETURN(_pinOn->write(onLevel));
      else if (on) {
        ESP_CHECK_RETURN(_pinOff->write(!offLevel));
        ESP_CHECK_RETURN(_pinOn->write(onLevel));
        delay(100);
        ESP_CHECK_RETURN(_pinOn->write(!onLevel));
        if (noSense)
          setState(State::On);
      } else {
        ESP_CHECK_RETURN(_pinOn->write(false));
        ESP_CHECK_RETURN(_pinOff->write(true));
        delay(100);
        ESP_CHECK_RETURN(_pinOff->write(false));
        if (noSense)
          setState(State::Off);
      }
      refreshState();
      return ESP_OK;
    }

    const char *Relay::toString(State s) {
      static const char *names[] = {"unknown", "on", "off"};
      int si = (int)s;
      if (si < 0 || si > 2)
        si = 0;
      return names[si];
    }

    const char *Relay::stateName() {
      return toString(refreshState());
    }

    Relay::State getStateFromPins(io::pin::IDigital *on, io::pin::IDigital *off,
                                  bool levelOn, bool levelOff) {
      bool onLevel = false, offLevel = false;
      if (on)
        if (on->read(onLevel) != ESP_OK)
          return Relay::State::Unknown;
      if (off) {
        if (off == on)
          offLevel = onLevel;
        else if (off->read(offLevel) != ESP_OK)
          return Relay::State::Unknown;
      }

      if (on == off || !off)
        return onLevel == levelOn ? Relay::State::On : Relay::State::Off;
      if (!on) {
        if (!off)
          return Relay::State::Unknown;
        return offLevel == levelOff ? Relay::State::Off : Relay::State::On;
      }

      bool isOn = onLevel == levelOn;
      bool isOff = offLevel == levelOff;
      if (isOn != isOff)
        return isOn ? Relay::State::On : Relay::State::Off;
      return Relay::State::Unknown;
    }

    Relay::State Relay::refreshState() {
      State state;
      if (_pinSenseOn || _pinSenseOff)
        state = getStateFromPins(_pinSenseOn, _pinSenseOff,
                                 getLevel(Pin::SenseOn, true),
                                 getLevel(Pin::SenseOff, true));
      else if (_pinOn == _pinOff)
        state = getStateFromPins(_pinOn, _pinOff, getLevel(Pin::On, true),
                                 getLevel(Pin::Off, true));
      else
        return _state;  // no sense pins
      setState(state);
      return _state;
    }

    void Relay::setState(const JsonVariantConst state,
                         DynamicJsonDocument **result) {
      JsonVariantConst v = state["state"];
      if (!v)
        v = state;
      if (v.is<const char *>())
        json::checkSetResult(turn(v.as<const char *>()), result);
    }

    DynamicJsonDocument *Relay::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
      JsonObject info = doc->to<JsonObject>();
      info["state"] = toString(refreshState());
      return doc;
    }

    esp_err_t Relay::turn(const char *action) {
      if (!strcmp(action, "on"))
        return turn(true);
      if (!strcmp(action, "off"))
        return turn(false);
      logW("unrecognized action: %s", action);
      return ESP_FAIL;
    }

    bool Relay::setConfig(const JsonVariantConst cfg,
                          DynamicJsonDocument **result) {
      if (isPersistent()) {
        setState(cfg, result);
        return true;
      }
      return false;
    }

    DynamicJsonDocument *Relay::getConfig(RequestContext &ctx) {
      if (isPersistent())
        return getState(ctx.request.data());
      return nullptr;
    }

    bool Relay::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      static const char *names[] = {"?", "ON", "OFF"};
      if (req.is(ha::DescribeRequest::Name)) {
        DynamicJsonDocument *doc = new DynamicJsonDocument(
            JSON_OBJECT_SIZE(1 + 2));  // acceptsCommands, component, config
        auto root = doc->to<JsonObject>();
        root["component"] = "switch";
        root.createNestedObject("config");
        req.respond(name(), doc->as<JsonVariantConst>());
        delete doc;
        return true;
      } else if (req.is(ha::StateRequest::Name)) {
        int si = (int)refreshState();
        if (si < 0 || si > 2)
          si = 0;
        DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
        auto root = doc->to<JsonObject>();
        root["state"] = names[si];
        req.respond(name(), doc->as<JsonVariantConst>());
        delete doc;
        return true;
      } else if (req.is(ha::CommandRequest::Name)) {
        auto state = req.data().as<const char *>();
        if (state) {
          // logD("received MQTT command: %s", state);
          if (!strcmp(state, names[(int)State::On]))
            turn(true);
          else if (!strcmp(state, names[(int)State::Off]))
            turn(false);
        }
        req.respond();
        return true;
      }
      return false;
    }

    Relay *useRelay(const char *name, io::IPin *pin) {
      return new Relay(name, pin->digital());
    }

    Relay *useRelay(const char *name, io::IPin *pinOn, io::IPin *pinOff) {
      return new Relay(name, pinOn->digital(), pinOff->digital());
    }

    Relay *useRelay(const char *name, io::IPin *pinOn, io::IPin *pinOff,
                    io::IPin *pinSenseOn, io::IPin *pinSenseOff) {
      return new Relay(name, pinOn->digital(), pinOff->digital(),
                       pinSenseOn->digital(), pinSenseOff->digital());
    }

  }  // namespace dev
}  // namespace esp32m