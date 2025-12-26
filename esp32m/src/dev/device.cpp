#include "esp32m/device.hpp"
#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/net/ota.hpp"
#include "esp32m/sleep.hpp"

#include <esp_task_wdt.h>
#include <math.h>

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
    void record(unsigned long next) {
      if (next < _next)
        _next = next;
    }
    unsigned long getNext() {
      return _next;
    }

   private:
    unsigned long _next = ULONG_MAX;
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
    if (EventPollSensorsTime::is(ev, &pste))
      pste->record(nextSensorsPollTime());
    else if (EventPollSensors::is(ev) && shouldPollSensors()) {
      if (sensorsReady() && !pollSensors())
        resetSensors();
      _sensorsPolledAt = millis();
      /*logI("sensors polled at %d, next poll at %d", _sensorsPolledAt,
           nextSensorsPollTime());*/
    }
  }

  /*  void Device::sensor(const char* sensor, const float value) {
      if (!isnan(value))
        EventSensor::publish(*this, sensor, value,
    json::null<JsonObjectConst>());
    };

    void Device::sensor(const char* sensor, const float value,
                        const JsonObjectConst props) {
      if (!isnan(value))
        EventSensor::publish(*this, sensor, value, props);
    };*/

  void Device::setupSensorPollTask() {
    static TaskHandle_t task = nullptr;
    // static Sleeper sleeper;
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
              int sleepTime = 0;
              esp_task_wdt_reset();
              if (App::initialized()) {
                EventPollSensorsTime pste;
                pste.publish();
                auto next = pste.getNext();
                auto current = millis();
                if (next < current) {
                  locks::Guard guard(net::ota::Name);
                  ev.publish();
                } else
                  sleepTime = next - current;  // sleeper.sleep();
                /*logi("next poll at %d, current time %d, sleep %d", next,
                     current, sleepTime);*/
              } else
                sleepTime = 1000;
              auto wdt = App::instance().wdtTimeout() - 100;
              if (sleepTime > wdt)
                sleepTime = wdt;
              if (sleepTime > 0)
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleepTime));
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
      union {
        // sensor::GroupChanged* gc;
        ComponentStateChanged* sc;
      };
      bool changed = false;
      if (EventInited::is(ev))
        xTaskCreate([](void* self) { ((StateEmitter*)self)->run(); }, "m/stem",
                    4096, this, 1, &_task);
      else /*if (sensor::GroupChanged::is(ev, &gc)) {
        for (auto sensor : gc->group())
          if (!sensor->disabled && filter(sensor)) {
            std::lock_guard guard(_queueMutex);
            _queue[sensor->uid()].sensor = sensor;
            changed = true;
          }
      } else */
        if (ComponentStateChanged::is(ev, &sc)) {
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
        if ((_flags & EmitFlags::Periodically) && shouldEmit()) {
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
              components.push_back(kv.second.component);
            _queue.clear();
          }
        }
        if (components.size()) {
          emit(components);
          components.clear();
        }
        auto current = _emittedAt = millis();
        auto sleepTime = nextTime() - current;
        /*logD("current %d, next %d, sleepTime=%d ", current, nextTime(),
             sleepTime);*/

        auto wdt = App::instance().wdtTimeout() - 100;
        if (sleepTime > wdt)
          sleepTime = wdt;
        if (sleepTime > 0)
          ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleepTime));
      }
    }

  }  // namespace dev

  namespace sensor {
    int _groupCounter = 0;
    int nextGroup() {
      // std::lock_guard lock(_componentsMutex);
      return ++_groupCounter;
    }

    /* Group::Iterator::Iterator(int id) : _id(id) {
       if (id < 0)
         _inner = dev::_components.end();
       else {
         _inner = dev::_components.begin();
         if (_inner->second->group != _id)
           next();
       }
     }

     void Group::Iterator::next() {
       for (;;) {
         _inner++;
         if (_inner == dev::_components.end() || _inner->second->group == _id)
           break;
       }
     }*/
  }  // namespace sensor

}  // namespace esp32m
