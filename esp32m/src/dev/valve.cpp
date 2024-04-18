#include "esp32m/dev/valve.hpp"
#include "esp32m/base.hpp"

#include <esp_task_wdt.h>
#include <limits>

namespace esp32m {
  namespace dev {
    namespace valve {
      esp_err_t TwoRelays::set(Cmd cmd) {
        switch (cmd) {
          case Cmd::Off:
            ESP_CHECK_RETURN(_power->turn(false));
            ESP_CHECK_RETURN(_direction->turn(false));
            break;
          case Cmd::Open:
            ESP_CHECK_RETURN(_direction->turn(true));
            ESP_CHECK_RETURN(_power->turn(true));
            break;
          case Cmd::Close:
            ESP_CHECK_RETURN(_direction->turn(false));
            ESP_CHECK_RETURN(_power->turn(true));
            break;
        }
        return ESP_OK;
      }
      esp_err_t HBridge::set(Cmd cmd) {
        switch (cmd) {
          case Cmd::Off:
            ESP_CHECK_RETURN(_hb->run(dev::HBridge::Mode::Off));
            break;
          case Cmd::Open:
            ESP_CHECK_RETURN(_hb->run(dev::HBridge::Mode::Forward));
            break;
          case Cmd::Close:
            ESP_CHECK_RETURN(_hb->run(dev::HBridge::Mode::Reverse));
            break;
        }
        return ESP_OK;
      }

      TwoPinSensor::TwoPinSensor(io::pin::IDigital *open,
                                 io::pin::IDigital *closed)
          : _open(open), _closed(closed) {
        open->setDirection(true, false);
        closed->setDirection(true, false);
      };

      esp_err_t TwoPinSensor::sense(State &state) {
        bool opened, closed;
        ESP_CHECK_RETURN(_open->read(opened));
        ESP_CHECK_RETURN(_closed->read(closed));
        opened = opened == _openLevel;
        closed = closed == _closedLevel;
        if (opened) {
          if (closed)
            state = State::Invalid;
          else
            state = State::Opened;
        } else if (closed)
          state = State::Closed;
        else
          state = State::Unknown;
        return ESP_OK;
      }

    }  // namespace valve

    Valve::Valve(const char *name, valve::IWiring *wiring,
                 valve::ISensor *sensor)
        : _name(name), _wiring(wiring), _sensor(sensor) {
      xTaskCreate([](void *self) { ((Valve *)self)->run(); }, "m/valve", 4096,
                  this, tskIDLE_PRIORITY + 1, &_task);
    };

    void Valve::setSamplesCount(int samples) {
      if (_samplesCount == samples)
        return;
      uint16_t *newSamples = nullptr;
      ;
      if (samples > 0) {
        newSamples = (uint16_t *)malloc(samples * sizeof(uint16_t) * 2);
        auto copySamples = std::min(_samplesCount, samples);
        if (copySamples > 0) {
          memcpy(newSamples, _samples, copySamples * sizeof(uint16_t));
          memcpy(newSamples + samples, _samples + _samplesCount,
                 copySamples * sizeof(uint16_t));
        }
        if (_siOpen >= samples)
          _siOpen = samples - 1;
        if (_siClose >= samples)
          _siClose = samples - 1;
      } else {
        _siOpen = 0;
        _siClose = 0;
      }
      if (_samples)
        free(_samples);
      _samples = newSamples;
      _samplesCount = samples;
    }

    esp_err_t Valve::turn(float value) {
      logI("turn %f", value);
      if (value < 0.5)
        set(valve::Cmd::Close, false);
      else
        set(valve::Cmd::Open, false);
      /*      _targetValue = value;
            xTaskNotifyGive(_task);*/
      return ESP_OK;
    }

    const char *StateNames[] = {"unknown", "open",    "close",
                                "invalid", "opening", "closing"};

    DynamicJsonDocument *Valve::getState(const JsonVariantConst args) {
      auto doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(3));
      JsonObject info = doc->to<JsonObject>();
      info["state"] = StateNames[(int)_state];
      info["value"] = _value;
      info["target"] = _targetValue;
      return doc;
    }

    void Valve::setState(const JsonVariantConst state,
                         DynamicJsonDocument **result) {
      esp_err_t err = ESP_OK;
      JsonVariantConst value = state["value"];
      if (value.is<float>() && value.as<float>() >= 0)
        err = turn(value.as<float>());
      else {
        const char *action = state["state"];
        if (!action)
          return;
        logI("set state %s", action);
        if (!strcmp(action, "open"))
          err = set(valve::Cmd::Open, false);
        else if (!strcmp(action, "close"))
          err = set(valve::Cmd::Close, false);
        else
          logW("unrecognized action: %s", action);
      }
      json::checkSetResult(ESP_ERROR_CHECK_WITHOUT_ABORT(err), result);
    }

    bool Valve::setConfig(const JsonVariantConst cfg,
                          DynamicJsonDocument **result) {
      if (_persistent) {
        setState(cfg, result);
        return true;
      }
      return false;
    }

    DynamicJsonDocument *Valve::getConfig(RequestContext &ctx) {
      if (_persistent) {
        auto doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
        JsonObject info = doc->to<JsonObject>();
        switch (_state) {
          case State::Opened:
          case State::Opening:
            info["state"] = "open";
            break;
          case State::Closed:
          case State::Closing:
            info["state"] = "close";
            break;
          default:
            break;
        }
        return doc;
      }
      return nullptr;
    }

    esp_err_t Valve::set(valve::Cmd cmd, bool wait) {
      ESP_CHECK_RETURN(_wiring->set(cmd));
      _startTime = millis();
      switch (cmd) {
        case valve::Cmd::Open:
          setState(Valve::State::Opening);
          break;
        case valve::Cmd::Close:
          setState(Valve::State::Closing);
          break;
        default:
          break;
      }

      if (wait)
        switch (cmd) {
          case valve::Cmd::Open:
            return waitFor(Valve::State::Opened);
          case valve::Cmd::Close:
            return waitFor(Valve::State::Closed);

          default:
            break;
        }
      else {
        xTaskNotifyGive(_task);
        ESP_CHECK_RETURN(refreshState());
      }
      return ESP_OK;
    }

    esp_err_t Valve::waitFor(Valve::State state) {
      esp_err_t err = ESP_OK;
      while (err == ESP_OK) {
        esp_task_wdt_reset();
        err = refreshState();
        if (err != ESP_OK)
          break;
        int elapsed = millis() - _startTime;
        if (elapsed > 10000)
          err = ESP_ERR_TIMEOUT;
        else if (_state == state) {
          logI("state changed to %d in %dms", _state, elapsed);
          break;
        }
      }
      if (err != ESP_OK)
        _wiring->set(valve::Cmd::Off);
      _startTime = 0;
      return err;
    }
    void Valve::setState(State state) {
      if (_state == state)
        return;
      switch (state) {
        case State::Unknown:
          _value = -1;
          break;
        case State::Opened:
          _value = 1;
          break;
        case State::Closed:
          _value = 0;
          break;
        case State::Invalid:
          _value = NAN;
          break;
        default:
          break;
      }
      //      logD("state transition %d->%d", _state, state);
      _state = state;
      if (isPersistent())
        switch (state) {
          case State::Opened:
          case State::Closed:
            config::Changed::publish(this);
            break;
          default:
            break;
        }
    }

    esp_err_t Valve::refreshState() {
      valve::ISensor::State ss;
      ESP_CHECK_RETURN(_sensor->sense(ss));
      switch (ss) {
        case valve::ISensor::State::Unknown:
          switch (_state) {
            case State::Unknown:
            case State::Opening:
            case State::Closing:
              return ESP_OK;
            default:
              setState(State::Unknown);
              return ESP_OK;
          }
        case valve::ISensor::State::Opened:
        case valve::ISensor::State::Closed:
        case valve::ISensor::State::Invalid:
          switch (_state) {
            case State::Opening:
            case State::Closing:
              if (millis() - _startTime <
                  3000)  // give it some time to change sensor state afer we
                         // started the motor
                return ESP_OK;
              break;
            default:
              break;
          }
          setState((State)ss);
          return _wiring->set(valve::Cmd::Off);
        default:
          return ESP_FAIL;
      }
    }

    void Valve::run() {
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        refreshState();
        auto delay = (_state > State::Invalid) ? 10 : 1000;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay));
      }
    }

    Valve *useValve(const char *name, valve::IWiring *wiring, io::IPin *isOpen,
                    io::IPin *isClosed) {
      return new Valve(
          name, wiring,
          new valve::TwoPinSensor(isOpen->digital(), isClosed->digital()));
    }

    Valve *useValve(const char *name, HBridge *hb, io::IPin *isOpen,
                    io::IPin *isClosed) {
      return new Valve(
          name, new valve::HBridge(hb),
          new valve::TwoPinSensor(isOpen->digital(), isClosed->digital()));
    }
    Valve *useValve(const char *name, Relay *power, Relay *direction,
                    io::IPin *isOpen, io::IPin *isClosed) {
      return new Valve(
          name, new valve::TwoRelays(power, direction),
          new valve::TwoPinSensor(isOpen->digital(), isClosed->digital()));
    }

  }  // namespace dev
}  // namespace esp32m