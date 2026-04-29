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
#include <cstring>
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
      constexpr const char* Select = "select";
      constexpr const char* Cover = "cover";
      constexpr const char* BinarySensor = "binary_sensor";
    }  // namespace ComponentType

    enum class StateClass { Undefined, Measurement, Total, TotalIncreasing };

    class Component : public virtual log::Loggable,
                      public virtual json::PropsContainer {
     public:
      enum Flags {
        None = 0,
        Disabled = 1 << 0,
        Sensor =
            1 << 1,  // instances of Sensor component type should set this flag
        RetainState = 1 << 2,  // if set, the component state will be retained
                               // in MQTT storage
        HasState = 1 << 3,     // if set, the component has state
        AcceptsCommands = 1 << 4  // if set, the component accepts commands
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
        return (_flags & Flags::Disabled) != 0;
      }

      void setDisabled(bool disabled) {
        if (disabled)
          _flags = (Flags)(_flags | Flags::Disabled);
        else
          _flags = (Flags)(_flags & ~Flags::Disabled);
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
        return (_flags & Flags::Sensor) != 0;
      }

      bool hasState() const {
        return (_flags & Flags::HasState) != 0;
      }

      bool shouldRetainState() const {
        return (_flags & Flags::RetainState) != 0;
      }

      bool acceptsCommands() const {
        return (_flags & Flags::AcceptsCommands) != 0;
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
      Flags _flags = Flags::None;
      Component(const char* id = nullptr) {
        if (id)
          _id = id;
      }
      void init(Device* device);

     private:
      template <typename T, typename Enable = void>
      struct StateChangeDetector {
        static bool hasChanged(JsonVariantConst oldValue, const T& value, int) {
          return !oldValue.is<T>() || oldValue.as<T>() != value;
        }
      };

      template <typename T>
      struct StateChangeDetector<
          T, typename std::enable_if<std::is_floating_point<T>::value>::type> {
        static bool hasChanged(JsonVariantConst oldValue, T value,
                               int precision) {
          if (!oldValue.is<T>())
            return true;
          auto effectivePrecision = precision;
          if (effectivePrecision < 0)
            effectivePrecision = std::numeric_limits<T>::digits10;
          T multiplier = std::pow(T(10), effectivePrecision);
          T rNew = std::round(value * multiplier);
          T rOld = std::round(oldValue.as<T>() * multiplier);
          return rNew != rOld;
        }
      };

      template <typename T>
      struct StateChangeDetector<
          T, typename std::enable_if<std::is_same<T, const char*>::value ||
                                     std::is_same<T, char*>::value>::type> {
        static bool hasChanged(JsonVariantConst oldValue, const T& value, int) {
          auto oldString = oldValue.as<const char*>();
          const char* newString = value ? value : "";
          return !oldString || std::strcmp(oldString, newString) != 0;
        }
      };

      template <typename T>
      struct StateChangeDetector<
          T,
          typename std::enable_if<std::is_same<T, std::string>::value>::type> {
        static bool hasChanged(JsonVariantConst oldValue, const T& value, int) {
          return !oldValue.is<const char*>() ||
                 oldValue.as<std::string>() != value;
        }
      };

      Device* _device = nullptr;
      std::unique_ptr<JsonDocument> _props;
    };

    ENUM_FLAG_OPERATORS(Component::Flags)

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
      if (!_device) {
        if (changed)
          *changed = false;
        return;
      }
      using ValueType = std::decay_t<T>;
      JsonVariantConst oldValue = _device->getComponentState(id());
      bool ch = StateChangeDetector<ValueType>::hasChanged(oldValue, value,
                                                           precision);
      if (changed)
        *changed = ch;
      if (ch) {
        _device->setComponentState(id(), value);
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
      StateClass stateClass = StateClass::Undefined;
      const char* unit = nullptr;
      Sensor(Device* device, const char* type, const char* id = nullptr)
          : Component(id), _type(type) {
        _flags = (Flags)(_flags | Flags::Sensor | Flags::HasState);
        Component::init(device);
      }

      Sensor(const Sensor&) = delete;
      const char* type() const override {
        return _type;
      }
      const char* component() const override {
        return ComponentType::Sensor;
      }

      const char* id() const override {
        if (!_id.empty())
          return _id.c_str();
        return _type;
      }

      bool is(const char* t) const {
        return t && !strcmp(type(), t);
      }
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

    class BinarySensor : public Sensor {
     public:
      BinarySensor(Device* device, const char* name, const char* id = nullptr)
          : Sensor(device, name, id) {
        _flags |= Flags::RetainState;
      }
      BinarySensor(const BinarySensor&) = delete;
      const char* component() const override {
        return ComponentType::BinarySensor;
      }
    };

    class Switch : public Component {
     public:
      Switch(Device* device, const char* id = nullptr) : Component(id) {
        Component::init(device);
        _flags |= Flags::RetainState | Flags::HasState | Flags::AcceptsCommands;
      }
      Switch(const Switch&) = delete;
      const char* type() const override {
        return "";
      }
      const char* component() const override {
        return ComponentType::Switch;
      }
      void set(bool value, bool* changed = nullptr) {
        setState(value, changed);
      }
      bool get() {
        return getState().as<bool>();
      }
    };

    class Select : public Component {
     public:
      typedef std::vector<std::string> TOptions;
      Select(Device* device, const TOptions& options, const char* id = nullptr)
          : Component(id), _options(options) {
        Component::init(device);
        _flags |= Flags::RetainState | Flags::HasState | Flags::AcceptsCommands;
      }
      Select(const Select&) = delete;
      const char* type() const override {
        return "";
      }
      const char* component() const override {
        return ComponentType::Select;
      }
      void set(std::string value, bool* changed = nullptr) {
        setState(value, changed);
      }
      std::string get() {
        return getState().as<std::string>();
      }
      const TOptions& options() const {
        return _options;
      }

     private:
      const TOptions& _options;
    };

    enum class CoverState { Unknown, Opening, Open, Closing, Closed, Stopped };

    class Cover : public Component {
     public:
      Cover(Device* device, const char* type, const char* id = nullptr)
          : Component(id), _type(type) {
        Component::init(device);
        _flags |= Flags::RetainState | Flags::HasState | Flags::AcceptsCommands;
      }
      Cover(const Cover&) = delete;
      const char* type() const override {
        return _type;
      }
      const char* component() const override {
        return ComponentType::Cover;
      }
      void set(CoverState value, int position, bool* changed = nullptr) {
        const char* strValue = stateName(value);
        if (!strValue) {
          resetState(changed);
          return;
        }
        JsonDocument doc;
        auto root = doc.to<JsonObject>();
        root["state"] = strValue;
        if (position >= 0)
          root["position"] = position;
        setState(doc.as<JsonVariantConst>(), changed);
      }
      void set(CoverState value, bool* changed = nullptr) {
        set(value, position(), changed);
      }
      void setPosition(int value, bool* changed = nullptr) {
        set(get(), value, changed);
      }
      CoverState get() const {
        auto state = getState();
        if (!state.is<JsonObjectConst>())
          return CoverState::Unknown;
        auto strValue = state["state"].as<std::string>();
        if (strValue == "opening")
          return CoverState::Opening;
        if (strValue == "open")
          return CoverState::Open;
        if (strValue == "closing")
          return CoverState::Closing;
        if (strValue == "closed")
          return CoverState::Closed;
        if (strValue == "stopped")
          return CoverState::Stopped;
        return CoverState::Unknown;
      }
      int position() const {
        auto state = getState();
        if (state.is<JsonObjectConst>() && state["position"].is<int>()) {
          auto value = state["position"].as<int>();
          if (value < 0)
            return 0;
          if (value > 100)
            return 100;
          return value;
        }
        switch (get()) {
          case CoverState::Open:
            return 100;
          case CoverState::Closed:
            return 0;
          default:
            return -1;
        }
      }

     private:
      const char* _type;
      static const char* stateName(CoverState value) {
        switch (value) {
          case CoverState::Opening:
            return "opening";
          case CoverState::Open:
            return "open";
          case CoverState::Closing:
            return "closing";
          case CoverState::Closed:
            return "closed";
          case CoverState::Stopped:
            return "stopped";
          default:
            return nullptr;
        }
      }
    };

  }  // namespace dev

  namespace sensor {
    int nextGroup();

  }  // namespace sensor

}  // namespace esp32m