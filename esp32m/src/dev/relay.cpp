#include <driver/gpio.h>

#include "esp32m/app.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/dev/relay.hpp"
#include "esp32m/integrations/ha/ha.hpp"

namespace esp32m {
  namespace dev {
    void Relay::init() {
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
      JsonDocument doc;
      doc.set(serialized(toString(state)));
      EventStateChanged::publish(this, doc);
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

    esp_err_t getStateFromPins(io::pin::IDigital *on, io::pin::IDigital *off,
                               bool levelOn, bool levelOff,
                               Relay::State &state) {
      bool onLevel = false, offLevel = false;
      if (on)
        ESP_CHECK_RETURN(on->read(onLevel));
      if (off) {
        if (off == on)
          offLevel = onLevel;
        else
          ESP_CHECK_RETURN(off->read(offLevel));
      }

      if (on == off || !off) {
        state = onLevel == levelOn ? Relay::State::On : Relay::State::Off;
        return ESP_OK;
      }
      if (!on) {
        if (!off)
          state = Relay::State::Unknown;
        else
          state = offLevel == levelOff ? Relay::State::Off : Relay::State::On;
        return ESP_OK;
      }

      bool isOn = onLevel == levelOn;
      bool isOff = offLevel == levelOff;
      if (isOn != isOff)
        state = isOn ? Relay::State::On : Relay::State::Off;
      else
        state = Relay::State::Unknown;
      return ESP_OK;
    }

    Relay::State Relay::refreshState() {
      State state = Relay::State::Unknown;
      esp_err_t err = ESP_OK;
      if (_pinSenseOn || _pinSenseOff)
        err = getStateFromPins(_pinSenseOn, _pinSenseOff,
                               getLevel(Pin::SenseOn, true),
                               getLevel(Pin::SenseOff, true), state);
      else if (_pinOn == _pinOff)
        err = getStateFromPins(_pinOn, _pinOff, getLevel(Pin::On, true),
                               getLevel(Pin::Off, true), state);
      else
        return _state;  // no sense pins
      if (err != ESP_OK)
        return _state;  // cannot sense
      setState(state);
      return _state;
    }

    void Relay::setState(RequestContext &ctx) {
      auto state = ctx.data;
      JsonVariantConst v = state["state"];
      if (!v)
        v = state;
      if (v.is<const char *>())
        ctx.errors.check(turn(v.as<const char *>()));
    }

    JsonDocument *Relay::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_OBJECT_SIZE(1) */
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

    bool Relay::setConfig(RequestContext &ctx) {
      if (isPersistent()) {
        setState(ctx);
        return true;
      }
      return false;
    }

    JsonDocument *Relay::getConfig(RequestContext &ctx) {
      if (isPersistent())
        return getState(ctx);
      return nullptr;
    }

    bool Relay::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is(integrations::ha::DescribeRequest::Name)) {
        JsonDocument *doc = new JsonDocument(); /* JSON_OBJECT_SIZE(6) */
        auto root = doc->to<JsonObject>();
        root["component"] = "switch";
        auto config = root["config"].to<JsonObject>();
        config["payload_on"] = "on";
        config["payload_off"] = "off";
        config["state_on"] = "on";
        config["state_off"] = "off";
        req.respond(name(), doc->as<JsonVariantConst>());
        delete doc;
        return true;
      } else if (req.is(integrations::ha::CommandRequest::Name)) {
        auto state = req.data().as<const char *>();
        if (state)
          turn(state);
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