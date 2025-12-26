#pragma once

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <map>
#include <mutex>

#include "esp32m/app.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/events.hpp"
#include "esp32m/json.hpp"

#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

namespace esp32m {

  class Device;

  class Device : public virtual AppObject, public virtual json::PropsContainer {
   public:
    enum Flags {
      None = 0,
      HasSensors = 1,
    };
    Device(const Device&) = delete;
    void setReinitDelay(unsigned int delay) {
      _reinitDelay = delay;
    }
    bool hasSensors() const {
      return (_flags & Flags::HasSensors) != 0;
    }

    void setSensorsPollInterval(int intervalMs) {
      _sensorsPollInterval = intervalMs;
    };
    int getSensorsPollInterval() const {
      return _sensorsPollInterval;
    }

    JsonVariantConst getComponentState(const char* id) const {
      std::lock_guard guard(_componentsStateMutex);
      if (_componentsState) {
        return id ? _componentsState->as<JsonObjectConst>()[id]
                  : JsonVariantConst{};
      }
      return JsonVariantConst{};
    }

    template <typename T>
    void setComponentState(const char* id, T state) {
      std::lock_guard guard(_componentsStateMutex);
      JsonObject container;
      if (!_componentsState) {
        _componentsState = std::make_unique<JsonDocument>();
        container = _componentsState->to<JsonObject>();
      } else
        container = _componentsState->as<JsonObject>();
      if (id)
        container[id] = state;
    }

    void setComponentState(const char* id, JsonVariantConst state) {
      std::lock_guard guard(_componentsStateMutex);
      JsonObject container;
      if (!_componentsState) {
        _componentsState = std::make_unique<JsonDocument>();
        container = _componentsState->to<JsonObject>();
      } else
        container = _componentsState->as<JsonObject>();
      if (id) {
        if (state.isUnbound())
          container.remove(id);
        else
          container[id] = state;
      } else {
        if (state.isUnbound())
          _componentsState.reset();
        else
          container.set(state);
      }
    }

   protected:
    Flags _flags = Flags::None;
    Device() {};
    void init(const Flags flags);
    void handleEvent(Event& ev) override;
    virtual bool initSensors() {
      return true;
    }
    virtual void resetSensors(bool immediate = false) {
      _sensorsReady = false;
      if (immediate)
        _sensorsInitAt = 0;
    }
    virtual bool pollSensors() {
      return true;
    };
    virtual unsigned long nextSensorsPollTime() {
      return _sensorsPolledAt + _sensorsPollInterval;
    };
    virtual bool shouldPollSensors() {
      return millis() >= nextSensorsPollTime();
    };

   private:
    bool _sensorsReady = false;
    unsigned long _sensorsInitAt = 0;
    unsigned long _sensorsPolledAt = 0;
    unsigned int _reinitDelay = 10000;
    int _sensorsPollInterval = 1000;
    mutable std::mutex _componentsStateMutex;
    std::unique_ptr<JsonDocument> _componentsState;
    bool sensorsReady();
    static void setupSensorPollTask();
  };

  ENUM_FLAG_OPERATORS(Device::Flags)

  namespace dev {

    namespace ComponentType {
      constexpr const char* Sensor = "sensor";
      constexpr const char* Switch = "switch";
      constexpr const char* BinarySensor = "binary_sensor";
    }  // namespace ComponentType

    enum class StateClass { Undefined, Measurement, Total, TotalIncreasing };

    class Component : public virtual log::Loggable,
                      public virtual json::PropsContainer {
     public:
      enum Options {
        None = 0,
        Disabled = 1 << 0,
        Sensor = 1 << 1,
        RetainState = 1 << 2,
      };
      int precision = -1;
      Component(const Component&) = delete;
      virtual ~Component();

      const char* name() const override {
        return id();
      }

      bool isComponent(const char* type) const {
        return type && !strcmp(component(), type);
      }

      Device* device() const {
        return _device;
      }

      bool isDisabled() const {
        return (_options & Options::Disabled) != 0;
      }

      void setDisabled(bool disabled) {
        if (disabled)
          _options = (Options)(_options | Options::Disabled);
        else
          _options = (Options)(_options & ~Options::Disabled);
      }

      // descriptive name, optional
      const char* title() const {
        return _title;
      }
      void setTitle(const char* title) {
        _title = title;
      }

      /**
       * @return true if this component is an instance of Sensor or its
       * subclass
       */
      bool isSensor() const {
        return (_options & Options::Sensor) != 0;
      }

      bool shouldRetainState() const {
        return (_options & Options::RetainState) != 0;
      }
      /**
       * @return component type
       */
      virtual const char* component() const = 0;
      /**
       * @return component class (usually equivalent of HA device_class)
       */
      virtual const char* type() const = 0;
      /**
       * @return component identifier that is unique in its device scope
       */
      virtual const char* id() const {
        if (!_id.empty())
          return _id.c_str();
        return component();
      }

      JsonVariantConst getState() const {
        return _device ? _device->getComponentState(id()) : JsonVariantConst{};
      }

      template <typename T>
      void setState(T value, bool* changed = nullptr);

      void setState(JsonVariantConst value, bool* changed = nullptr);

      void resetState(bool* changed = nullptr) {
        setState(JsonVariantConst{}, changed);
      }

      void exportState(JsonObject target) const {
        auto value = getState();
        if (precision >= 0) {
          if (value.is<float>())
            target[id()] =
                serialized(roundToString(value.as<float>(), precision));
          else if (value.is<double>())
            target[id()] =
                serialized(roundToString(value.as<double>(), precision));
          else if (value.is<long double>())
            target[id()] =
                serialized(roundToString(value.as<long double>(), precision));
          else
            target[id()] = value;
        } else
          target[id()] = value;
      }

      /**
       * @return sensor identifier that is unique in the project scope
       */
      std::string uid() const {
        std::string uid(_device->name());
        uid += "_";
        uid += id();
        return uid;
      }

      JsonObjectConst props() const override {
        return _props ? _props->as<JsonObjectConst>()
                      : json::null<JsonObjectConst>();
      }
      void setProps(JsonDocument* props) {
        _props.reset(props);
      }

      static Component* find(std::string uid);
      static Component* find(Device* device, const char* id);

     protected:
      std::string _id;
      const char* _title = nullptr;
      Options _options = Options::None;
      Component(const char* id = nullptr) {
        if (id)
          _id = id;
      }
      void init(Device* device);

     private:
      Device* _device = nullptr;
      std::unique_ptr<JsonDocument> _props;
    };

    ENUM_FLAG_OPERATORS(Component::Options)

    class ComponentStateChanged : public Event {
     public:
      ComponentStateChanged(const ComponentStateChanged&) = delete;
      const Component* component() const {
        return _component;
      }
      static void publish(const Component* component) {
        ComponentStateChanged ev(component);
        ev.Event::publish();
      }
      static bool is(Event& ev, ComponentStateChanged** changed) {
        if (ev.is(Type)) {
          if (changed)
            *changed = (ComponentStateChanged*)&ev;
          return true;
        }
        return false;
      }

     private:
      ComponentStateChanged(const Component* component)
          : Event(Type), _component(component) {}
      const Component* _component;
      constexpr static const char* Type = "component-state-changed";
    };

    template <typename T>
    void Component::setState(T value, bool* changed) {
      if (!_device)
        return;
      JsonVariantConst oldValue = _device->getComponentState(id());
      bool ch;
      if (std::is_floating_point_v<T> && oldValue.is<T>()) {
        auto effectivePrecision = precision;
        static T epsilon = 0.5;
        if (effectivePrecision < 0)
          effectivePrecision = std::numeric_limits<T>::digits10;
        T multiplier = std::pow(10.0, effectivePrecision);
        T rNew = std::round(value * multiplier);
        T rOld = std::round(oldValue.as<T>() * multiplier);
        ch = std::abs(rNew - rOld) >= epsilon;
      } else {
        ch = oldValue != value;
      }
      if (ch) {
        _device->setComponentState(id(), value);
        if (changed)
          *changed = true;
        ComponentStateChanged::publish(this);
      }
    }

    class AllComponents {
     public:
      struct Iterator {
       public:
        typedef std::map<std::string, Component*>::iterator Inner;
        Iterator(Inner inner) : _inner(inner) {}
        bool operator==(const Iterator& other) const {
          return _inner == other._inner;
        }
        bool operator!=(const Iterator& other) const {
          return _inner != other._inner;
        }
        Component* operator*() const {
          return _inner->second;
        }
        Iterator& operator++() {
          _inner++;
          return *this;
        }

       private:
        Inner _inner;
        void next();
      };
      Iterator begin() const;
      Iterator end() const;
    };

    enum EmitFlags { None = 0, Periodically = 1 << 0, OnChange = 1 << 1 };

    ENUM_FLAG_OPERATORS(EmitFlags)
    struct QueueItem {
      const Component* component;
    };

    class StateEmitter : public AppObject {
     public:
      StateEmitter(const StateEmitter&) = delete;
      StateEmitter(EmitFlags flags = EmitFlags::Periodically |
                                     EmitFlags::OnChange);
      void setInterval(int intervalMs) {
        _interval = intervalMs;
      };
      int getInterval() const {
        return _interval;
      };
      virtual unsigned long nextTime() {
        auto current = millis();
        auto next = _emittedAt + _interval;
        return next > current ? next : current;
      };
      virtual bool shouldEmit() {
        return millis() >= nextTime();
      };

     protected:
      void handleEvent(Event& ev) override;
      virtual void emit(std::vector<const Component*> components) = 0;
      virtual bool filter(const Component* component) {
        return true;
      }

     private:
      EmitFlags _flags;
      int _interval = 1000;
      unsigned long _emittedAt = 0;
      TaskHandle_t _task = nullptr;
      std::mutex _queueMutex;
      std::map<std::string, QueueItem> _queue;
      void run();
    };

    class Sensor : public Component {
     public:
      int group = 0;
      StateClass stateClass = StateClass::Undefined;
      const char* unit = nullptr;
      Sensor(Device* device, const char* type, const char* id = nullptr)
          : Component(id), _type(type) {
        _options = (Options)(_options | Options::Sensor);
        Component::init(device);
      }

      Sensor(const Sensor&) = delete;
      const char* type() const override {
        return _type;
      }
      const char* component() const override {
        return "sensor";
      }

      const char* id() const override {
        if (!_id.empty())
          return _id.c_str();
        return _type;
      }

      bool is(const char* t) const {
        return t && !strcmp(type(), t);
      }
      /*template <typename T>
      void set(T value, bool* changed = nullptr) {
        if constexpr (std::is_same_v<T, float>)
          if (precision >= 0)
            value = roundTo(value, precision);
        auto c = _value != value;
        _value.set(value);
        if (c) {
          if (changed)
            *changed = true;
          if (group <= 0)
            ComponentStateChanged::publish(this);
        }
      }*/
      JsonVariantConst get() const {
        return getState();
      }
      template <typename T>
      void set(T value, bool* changed = nullptr) {
        setState(value, changed);
      }

     private:
      const char* _type;
    };

    class Switch : public Component {
     public:
      Switch(Device* device, const char* id = nullptr) : Component(id) {
        Component::init(device);
        _options |= Options::RetainState;
      }
      Switch(const Switch&) = delete;
      const char* type() const override {
        return "";
      }
      const char* component() const override {
        return "switch";
      }
      void set(bool value, bool* changed = nullptr) {
        setState(value, changed);
      }
      bool get() {
        return getState().as<bool>();
      }
    };

    class BinarySensor : public Sensor {
     public:
      BinarySensor(Device* device, const char* name, const char* id = nullptr)
          : Sensor(device, name, id) {
        _options |= Options::RetainState;
      }
      BinarySensor(const BinarySensor&) = delete;
      const char* component() const override {
        return "binary_sensor";
      }
    };

  }  // namespace dev

  namespace sensor {
    int nextGroup();
    class Group {
     public:
      Group(int id) : _id(id) {}
      struct Iterator {
       public:
        typedef std::map<std::string, dev::Component*>::iterator Inner;
        Iterator(int id);
        bool operator==(const Iterator& other) const {
          return _inner == other._inner;
        }
        bool operator!=(const Iterator& other) const {
          return _inner != other._inner;
        }
        dev::Component* operator*() const {
          return _inner->second;
        }
        Iterator& operator++() {
          next();
          return *this;
        }

       private:
        int _id;
        Inner _inner;
        void next();
      };
      Iterator begin() const {
        return Iterator(_id);
      }
      Iterator end() const {
        return Iterator(-1);
      }

     private:
      int _id;
    };
    class GroupChanged : public Event {
     public:
      GroupChanged(const GroupChanged&) = delete;
      const Group& group() const {
        return _group;
      }
      static void publish(int group) {
        GroupChanged ev(group);
        ev.Event::publish();
      }
      static bool is(Event& ev, GroupChanged** changed) {
        if (ev.is(Type)) {
          if (changed)
            *changed = (GroupChanged*)&ev;
          return true;
        }
        return false;
      }

     private:
      GroupChanged(int group) : Event(Type), _group(group) {}
      Group _group;
      constexpr static const char* Type = "sensor-group-changed";
    };

  }  // namespace sensor

}  // namespace esp32m