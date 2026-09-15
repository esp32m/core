#include "esp32m/dev/valve.hpp"
#include "esp32m/base.hpp"
#include "esp32m/integrations/ha/ha.hpp"

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
          default:
            return ESP_ERR_INVALID_ARG;
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
          default:
            return ESP_ERR_INVALID_ARG;
        }
        return ESP_OK;
      }

      TwoPinSensor::TwoPinSensor(io::pin::IDigital *open,
                                 io::pin::IDigital *closed)
          : _open(open), _closed(closed) {};

      esp_err_t TwoPinSensor::sense(State &state) {
        state = State::Unknown;
        if (!_open || !_closed)
          return ESP_ERR_INVALID_ARG;
        if (!_configured) {
          ESP_CHECK_RETURN(_open->setDirection(true, false));
          ESP_CHECK_RETURN(_closed->setDirection(true, false));
          _configured = true;
        }
        // A PCA provider reads both limits from the same input-register snapshot.
        io::pin::Tx tx(io::pin::Tx::Read);
        bool opened, closed;
        ESP_CHECK_RETURN(_open->read(opened));
        ESP_CHECK_RETURN(_closed->read(closed));
        ESP_CHECK_RETURN(tx.commit());
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
        : _name(name), _wiring(wiring), _sensor(sensor), _component(this, "") {
      // The worker may start on the other CPU before xTaskCreate returns.
      std::lock_guard guard(_mutex);
      if (xTaskCreate([](void *self) { ((Valve *)self)->run(); }, "m/valve", 4096,
                      this, tskIDLE_PRIORITY + 1, &_task) != pdPASS)
        fail(ESP_ERR_NO_MEM, "worker-start");
    };

    void Valve::setSamplesCount(int samples) {
      std::lock_guard guard(_mutex);
      samples = std::max(0, samples);
      if (_samplesCount == samples)
        return;
      uint16_t *newSamples = nullptr;
      if (samples > 0) {
        newSamples = (uint16_t *)malloc(samples * sizeof(uint16_t) * 2);
        if (!newSamples)
          return;
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
      if (!std::isfinite(value) || value < 0 || value > 1)
        return ESP_ERR_INVALID_ARG;
      logI("turn %f", value);
      if (value < 0.5)
        return set(valve::Cmd::Close, false);
      return set(valve::Cmd::Open, false);
    }

    const char *StateNames[] = {"unknown", "open",    "close",
                                "invalid", "opening", "closing"};

    JsonDocument *Valve::getState(RequestContext &ctx) {
      std::lock_guard guard(_mutex);
      auto doc = new JsonDocument(); /* JSON_OBJECT_SIZE(3) */
      JsonObject info = doc->to<JsonObject>();
      info["state"] = StateNames[(int)_state];
      info["value"] = _value;
      info["target"] = _targetValue;
      info["fault"] = _fault;
      info["error"] = _error;
      if (_error != ESP_OK)
        info["errorName"] = esp_err_to_name(_error);
      info["stopPending"] = _stopPending;
      info["stopError"] = _stopError;
      if (_stopError != ESP_OK)
        info["stopErrorName"] = esp_err_to_name(_stopError);
      return doc;
    }

    void Valve::setState(RequestContext &ctx) {
      esp_err_t err = ESP_OK;
      auto state = ctx.data.as<JsonObjectConst>();
      JsonVariantConst value = state["value"];
      if (value.is<float>())
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
        else if (!strcmp(action, "off") || !strcmp(action, "stop"))
          err = set(valve::Cmd::Off, false);
        else {
          logW("unrecognized action: %s", action);
          err = ESP_ERR_INVALID_ARG;
        }
      }
      ctx.errors.check(ESP_ERROR_CHECK_WITHOUT_ABORT(err));
    }

    bool Valve::setConfig(RequestContext &ctx) {
      if (isPersistent()) {
        setState(ctx);
        return ctx.errors.empty();
      }
      return false;
    }

    JsonDocument *Valve::getConfig(RequestContext &ctx) {
      std::lock_guard guard(_mutex);
      if (_persistent) {
        auto doc = new JsonDocument(); /* JSON_OBJECT_SIZE(1) */
        JsonObject info = doc->to<JsonObject>();
        switch (_error == ESP_OK && !_stopPending ? _state : State::Unknown) {
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
      if (cmd != valve::Cmd::Off && cmd != valve::Cmd::Open &&
          cmd != valve::Cmd::Close)
        return ESP_ERR_INVALID_ARG;
      uint32_t commandId;
      const auto target = cmd == valve::Cmd::Open ? State::Opened : State::Closed;
      {
        std::lock_guard guard(_mutex);
        if (cmd != valve::Cmd::Off && !_task)
          return fail(ESP_ERR_INVALID_STATE, "worker-unavailable");
        if (cmd == _command && cmd != valve::Cmd::Off && _error == ESP_OK) {
          // Repeated requests must not extend an in-progress travel deadline.
          ESP_CHECK_RETURN(refreshState());
        } else {
          ++_commandId;
          auto err = stop();
          if (err != ESP_OK)
            return fail(err, "stop", false);
          valve::ISensor::State sensed;
          err = _sensor->sense(sensed);
          if (err != ESP_OK)
            return fail(err, "sensor", false);  // Off already confirmed.
          if (sensed == valve::ISensor::State::Invalid)
            return fail(ESP_ERR_INVALID_STATE, "limits", false);
          // Only a fresh valid command acknowledges a latched fault.
          _error = ESP_OK;
          _fault = nullptr;
          setState(static_cast<State>(sensed));
          if (cmd == valve::Cmd::Off)
            return ESP_OK;
          _targetValue = cmd == valve::Cmd::Open ? 1 : 0;
          if (_state == target) {
            _targetValue = NAN;
            return ESP_OK;
          }
          _command = cmd;
          _startTime = esp_timer_get_time() / 1000;
          err = _wiring->set(cmd);
          if (err != ESP_OK)
            // A failed bus write may still have reached the output latch.
            return fail(err, "start");
          setState(cmd == valve::Cmd::Open ? State::Opening : State::Closing);
        }
        commandId = _commandId;
        xTaskNotifyGive(_task);
      }
      // Never hold the valve mutex while sleeping for a movement to complete.
      return wait ? waitFor(target, commandId) : ESP_OK;
    }

    esp_err_t Valve::waitFor(Valve::State state, uint32_t commandId) {
      for (;;) {
        esp_task_wdt_reset();
        {
          std::lock_guard guard(_mutex);
          if (_commandId != commandId)
            return ESP_ERR_INVALID_STATE;  // Superseded by another command.
          ESP_CHECK_RETURN(refreshState());
          if (_error != ESP_OK)
            return _error;
          if (_command == valve::Cmd::Off && !_stopPending)
            return _state == state ? ESP_OK : ESP_ERR_INVALID_STATE;
        }
        vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(10)));
      }
    }

    esp_err_t Valve::stop() {
      _command = valve::Cmd::Off;
      _targetValue = NAN;
      _lastStopTime = esp_timer_get_time() / 1000;
      _stopError = _wiring->set(valve::Cmd::Off);
      _stopPending = _stopError != ESP_OK;
      return _stopError;
    }

    esp_err_t Valve::fail(esp_err_t error, const char *fault, bool tryStop) {
      if (_error == ESP_OK) {
        _error = error;
        _fault = fault;
        logW("valve fault %s: %s", fault, esp_err_to_name(error));
      }
      _command = valve::Cmd::Off;
      _targetValue = NAN;
      setState(State::Unknown);
      if (tryStop)
        stop();
      // Keep the first cause and a separate last stop error for diagnosis.
      return error;
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
      switch (state) {
        case State::Opening:
          _component.set(ValveState::Opening);
          break;
        case State::Opened:
          _component.set(ValveState::Open);
          break;
        case State::Closing:
          _component.set(ValveState::Closing);
          break;
        case State::Closed:
          _component.set(ValveState::Closed);
          break;
        default:
          _component.resetState();
          break;
      }
      if (isPersistent() && _error == ESP_OK && !_stopPending)
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
      std::lock_guard guard(_mutex);
      auto now = static_cast<uint64_t>(esp_timer_get_time() / 1000);
      if (_stopPending) {
        if (_stopError == ESP_OK || now - _lastStopTime >= 250) {
          auto err = stop();
          if (err != ESP_OK)
            return fail(err, "stop", false);
        }
        if (_stopPending)
          return _stopError;
      }
      // The worker enforces this for asynchronous UI/HA commands too.
      if (_command != valve::Cmd::Off && now - _startTime >= 10000)
        return fail(ESP_ERR_TIMEOUT, "travel-timeout");
      valve::ISensor::State ss;
      auto err = _sensor->sense(ss);
      if (err != ESP_OK) {
        // Once stopped, a continuing read fault need not keep rewriting Off.
        return fail(err, "sensor", _command != valve::Cmd::Off);
      }
      if (ss == valve::ISensor::State::Invalid)
        return fail(ESP_ERR_INVALID_STATE, "limits", _command != valve::Cmd::Off);
      if (_command != valve::Cmd::Off) {
        auto target = _command == valve::Cmd::Open ? valve::ISensor::State::Opened
                                                   : valve::ISensor::State::Closed;
        if (ss == target) {
          err = stop();
          if (err != ESP_OK)
            return fail(err, "stop", false);
          setState(static_cast<State>(ss));
        } else if (ss != valve::ISensor::State::Unknown &&
                   now - _startTime >= 3000) {
          // The departure limit should have released within the old 3s grace.
          return fail(ESP_ERR_INVALID_STATE, "departure-limit");
        }
      } else {
        setState(static_cast<State>(ss));
      }
      return ESP_OK;
    }

    void Valve::run() {
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        int delay;
        {
          std::lock_guard guard(_mutex);
          refreshState();
          delay = _stopPending ? 250 : _command != valve::Cmd::Off ? 10 : 1000;
        }
        ulTaskNotifyTake(pdTRUE, std::max<TickType_t>(1, pdMS_TO_TICKS(delay)));
      }
    }

    bool Valve::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is(integrations::ha::CommandRequest::Name)) {
        esp_err_t err = ESP_ERR_INVALID_ARG;
        auto cmd = req.data().as<const char *>();
        if (cmd) {
          if (!strcasecmp(cmd, "open"))
            err = set(valve::Cmd::Open, false);
          else if (!strcasecmp(cmd, "close"))
            err = set(valve::Cmd::Close, false);
          else if (!strcasecmp(cmd, "stop") || !strcasecmp(cmd, "off"))
            err = set(valve::Cmd::Off, false);
          else
            logW("unrecognized valve command: %s", cmd);
        }
        req.respond(err);
        return true;
      }
      return false;
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
