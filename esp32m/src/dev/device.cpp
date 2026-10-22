#include "esp32m/device.hpp"
#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/net/ota.hpp"
#include "esp32m/sleep.hpp"

#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <math.h>

#include <algorithm>

namespace esp32m {

  class EventPollSensors : public Event {
   public:
    EventPollSensors() : Event(Type) {}
    static bool is(Event& ev) {
      return ev.is(Type);
    }

   private:
    constexpr static const char* Type = "poll-sensors";
  };

  class EventPollSensorsTime : public Event {
   public:
    EventPollSensorsTime() : Event(Type) {}
    static bool is(Event& ev, EventPollSensorsTime** pste) {
      bool result = ev.is(Type);
      if (result && pste)
        *pste = (EventPollSensorsTime*)&ev;
      return result;
    }
    void record(uint64_t next) {
      if (next < _next)
        _next = next;
    }
    uint64_t getNext() {
      return _next;
    }

   private:
    uint64_t _next = UINT64_MAX;
    constexpr static const char* Type = "poll-sensors-time";
  };

  void Device::init(Flags flags) {
    _flags = flags;
    if (flags & Flags::HasSensors)
      setupSensorPollTask();
  }

  void Device::handleEvent(Event& ev) {
    if ((_flags & Flags::HasSensors) == 0)
      return;
    EventPollSensorsTime* pste;
    const bool time = EventPollSensorsTime::is(ev, &pste);
    if (!time && !EventPollSensors::is(ev)) return;
    // Configuration/manual I/O and subscription captures must not stall the
    // shared sensor task. Serialize each complete poll with state reads so a
    // snapshot cannot observe only part of a multi-component sample.
    std::unique_lock guard(_deviceMutex, std::defer_lock);
    if (!guard.try_lock()) return;
    if (isDisabled()) return;
    if (time)
      pste->record(nextSensorsPollTime());
    else if (EventPollSensors::is(ev) && shouldPollSensors()) {
      if (sensorsReady() && !pollSensors())
        resetSensors();
      _sensorsPolledAt = esp_timer_get_time() / 1000ULL;
      guard.unlock();
      // One hint per coherent observation, including unchanged successful
      // samples, initialization failure and diagnostic-only changes. A UI
      // listener only marks dirty; it never serializes from this callback.
      EventStateChanged::publish(this);
    }
  }

  bool Device::shouldPollSensors() {
    return !isDisabled() &&
           esp_timer_get_time() / 1000ULL >= nextSensorsPollTime();
  }

  bool Device::handleRequest(Request& request) {
    std::lock_guard guard(_deviceMutex);
    if ((_flags & Flags::CanDisable) != 0) {
      if (request.is("device-state")) {
        JsonDocument doc;
        doc["_disabled"] = isDisabled();
        request.respond(doc.as<JsonVariantConst>(), false);
        return true;
      }
      if (isDisabled() && !request.is("config-get") &&
          !request.is("config-set") && !request.is(KeyStateGet) &&
          !request.is(KeyInfoGet)) {
        request.respondError(ESP_ERR_INVALID_STATE,
                             "Device is disabled in settings");
        return true;
      }
    }
    return AppObject::handleRequest(request);
  }

  JsonDocument* Device::getConfig(RequestContext& ctx) {
    std::lock_guard guard(_deviceMutex);
    // A disable toggle is entirely local, including its config-set response.
    // Do not call a driver's on-demand register reads for this response.
    auto doc = ctx.data["_disabled"].isUnbound() || ctx.request.isInternal()
                   ? getDeviceConfig(ctx) : nullptr;
    if ((_flags & Flags::CanDisable) != 0) {
      if (!doc) doc = new JsonDocument();
      (*doc)["_disabled"] = isDisabled();
    }
    return doc;
  }

  bool Device::setConfig(RequestContext& ctx) {
    std::lock_guard guard(_deviceMutex);
    if ((_flags & Flags::CanDisable) == 0) return setDeviceConfig(ctx);
    const auto value = ctx.data["_disabled"];
    const bool internal = ctx.request.isInternal();
    if (!value.isUnbound() && (!value.is<bool>() ||
        (!internal && ctx.data.size() != 1))) {
      ctx.errors.check(ESP_ERR_INVALID_ARG,
                       "Set _disabled as a boolean in a separate request");
      return false;
    }
    if (!internal && value.isUnbound() && isDisabled()) {
      ctx.errors.check(ESP_ERR_INVALID_STATE,
                       "Enable the device before changing hardware settings");
      return false;
    }
    bool changed = false;
    if (!value.isUnbound()) {
      const bool disabled = value.as<bool>();
      changed = disabled != isDisabled();
      if (changed) {
        _disabled.store(disabled);
        _sampleGeneration.fetch_add(1);
        resetSensors(true);
        _sensorsPolledAt = 0;
        // Discard cached component values; re-enable must publish fresh data.
        setComponentState(nullptr, JsonVariantConst{});
        if (!disabled && hasSensors()) setupSensorPollTask();
        EventStateChanged::publish(this);
      }
      if (!internal) {
        ctx.result = std::make_unique<JsonDocument>();
        (*ctx.result)["_disabled"] = disabled;
        return changed;
      }
    }
    return setDeviceConfig(ctx) || changed;
  }

  void Device::setupSensorPollTask() {
    static TaskHandle_t task = nullptr;
    static EventPollSensors ev;
    if (task)
      xTaskNotifyGive(task);
    else
      xTaskCreate(
          [](void*) {
            EventManager::instance().subscribe([](Event& ev) {
              if (EventInited::is(ev) && task)
                xTaskNotifyGive(task);
            });
            esp_task_wdt_add(NULL);
            for (;;) {
              uint64_t sleepTime = 0;
              esp_task_wdt_reset();
              if (App::initialized()) {
                EventPollSensorsTime pste;
                pste.publish();
                auto next = pste.getNext();
                auto current = esp_timer_get_time() / 1000ULL;
                if (next <= current) {
                  locks::Guard guard(net::ota::Name);
                  ev.publish();
                } else
                  sleepTime = next - current;
              } else
                sleepTime = 1000;
              // wdtTimeout() is in seconds. Wake at least once per second,
              // including when there are no deadlines (UINT64_MAX), and cap
              // the 64-bit duration before converting it to RTOS ticks.
              const uint64_t wdtMs =
                  uint64_t(App::instance().wdtTimeout()) * 1000;
              const uint64_t maxSleep =
                  wdtMs > 100 ? std::min<uint64_t>(1000, wdtMs - 100) : 1;
              sleepTime = std::min<uint64_t>(sleepTime, maxSleep);
              if (sleepTime > 0) {
                const auto ticks = pdMS_TO_TICKS(uint32_t(sleepTime));
                ulTaskNotifyTake(pdTRUE, std::max<TickType_t>(1, ticks));
              }
            }
          },
          "m/sensors", 4096, nullptr, 1, &task);
  }

  bool Device::sensorsReady() {
    if (_sensorsReady)
      return true;
    unsigned int ms = millis();
    if (_sensorsInitAt && ms - _sensorsInitAt < _reinitDelay)
      return false;
    _sensorsInitAt = ms;
    _sensorsReady = initSensors();
    if (_sensorsReady)
      logI("device initialized");
    return _sensorsReady;
  }

  namespace dev {

    std::mutex _componentsMutex;
    std::map<std::string, Component*> _components;

    void Component::init(Device* device) {
      _device = device;
      std::lock_guard lock(_componentsMutex);
      if (_components.find(uid()) != _components.end()) {
        logW("component with uid %s already exists", uid().c_str());
      }
      _components[uid()] = this;
    }

    Component::~Component() {
      if (!_device)
        return;
      std::lock_guard lock(_componentsMutex);
      auto uid = this->uid();
      auto it = _components.find(uid);
      if (it != _components.end())
        _components.erase(it);
      _device = nullptr;
    }

    void Component::setState(JsonVariantConst value, bool* changed) {
      if (!_device)
        return;
      JsonVariantConst oldValue = _device->getComponentState(id());
      bool ch = oldValue != value;
      if (ch) {
        _device->setComponentState(id(), value);
        if (changed)
          *changed = true;
        ComponentStateChanged::publish(this);
      }
    }

    Component* Component::find(std::string uid) {
      std::lock_guard lock(_componentsMutex);
      auto it = _components.find(uid);
      return it == _components.end() ? nullptr : it->second;
    }

    Component* Component::find(Device* device, const char* id) {
      auto uid = string_printf("%s_%s", device->name(), id);
      return find(uid);
    }

    AllComponents::Iterator AllComponents::begin() const {
      return AllComponents::Iterator(_components.begin());
    }
    AllComponents::Iterator AllComponents::end() const {
      return AllComponents::Iterator(_components.end());
    }

    StateEmitter::StateEmitter(EmitFlags flags) : _flags(flags) {}

    void StateEmitter::handleEvent(Event& ev) {
      ComponentStateChanged* sc;
      bool changed = false;
      if (EventInited::is(ev))
        xTaskCreate([](void* self) { ((StateEmitter*)self)->run(); }, "m/stem",
                    4096, this, 1, &_task);
      else if (ComponentStateChanged::is(ev, &sc)) {
        auto component = sc->component();
        if (!component->isDisabled() && filter(component)) {
          std::lock_guard guard(_queueMutex);
          _queue[component->uid()].component = component;
          changed = true;
        }
      }
      if (changed && _task && (_flags & EmitFlags::OnChange))
        xTaskNotifyGive(_task);
    }

    void StateEmitter::run() {
      std::vector<const Component*> components;
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        const bool periodic = (_flags & EmitFlags::Periodically) && shouldEmit();
        if (periodic) {
          AllComponents all;
          for (auto component : all)
            if (!component->isDisabled() && filter(component)) {
              components.push_back(component);
              std::lock_guard guard(_queueMutex);
              auto queued = _queue.find(component->uid());
              if (queued != _queue.end())
                _queue.erase(queued);
            }
        } else {
          std::lock_guard guard(_queueMutex);
          if (_queue.size()) {
            for (auto const& kv : _queue)
              if (!kv.second.component->isDisabled())
                components.push_back(kv.second.component);
            _queue.clear();
          }
        }
        if (components.size()) {
          emit(components);
          components.clear();
        }
        auto current = millis();
        // Change-only sends and early wakes must not postpone the full snapshot.
        if (periodic || !(_flags & EmitFlags::Periodically))
          _emittedAt = current;
        auto sleepTime = nextTime() - current;
        // Match the sensor poller: watchdog timeout is seconds, not milliseconds.
        const uint64_t wdtMs = uint64_t(App::instance().wdtTimeout()) * 1000;
        const uint64_t maxSleep =
            wdtMs > 100 ? std::min<uint64_t>(1000, wdtMs - 100) : 1;
        sleepTime = std::min<uint64_t>(sleepTime, maxSleep);
        const auto ticks = std::max<TickType_t>(1, pdMS_TO_TICKS(sleepTime));
        ulTaskNotifyTake(pdTRUE, ticks);
      }
    }

  }  // namespace dev

}  // namespace esp32m
