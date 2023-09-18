#pragma once

#include <ArduinoJson.h>

#include "esp32m/app.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/events.hpp"
#include "esp32m/json.hpp"

#include <type_traits>

namespace esp32m {

  class Device;
  class Sensor;

  class EventSensor : public Event, public json::PropsContainer {
   public:
    EventSensor(const Device &device, const char *sensor, const float value,
                const JsonObjectConst props)
        : Event(NAME),
          _device(device),
          _sensor(sensor),
          _props(props),
          _value(value) {}
    const Device &device() const {
      return _device;
    }
    const char *sensor() const {
      return _sensor;
    }
    const JsonObjectConst props() const override {
      return _props;
    }
    float value() {
      return _value;
    }
    static void publish(const Device &device, const char *sensor,
                        const float value, const JsonObjectConst props) {
      EventSensor ev(device, sensor, value, props);
      ev.Event::publish();
    }
    static bool is(Event &ev) {
      return ev.is(NAME);
    }

   private:
    const Device &_device;
    const char *_sensor;
    const JsonObjectConst _props;
    const float _value;
    static const char *NAME;
  };

  class Device : public virtual AppObject, public virtual json::PropsContainer {
   public:
    enum Flags {
      None = 0,
      HasSensors = 1,
    };
    Device(const Device &) = delete;
    void setReinitDelay(unsigned int delay) {
      _reinitDelay = delay;
    }
    bool hasSensors() const {
      return (_flags & Flags::HasSensors) != 0;
    }
    /* these must be called only from within pollSensors() */
    void sensor(const char *sensor, const float value);
    void sensor(const char *sensor, const float value,
                const JsonObjectConst props);

    void setSensorsPollInterval(int intervalMs) {
      _sensorsPollInterval = intervalMs;
    };
    int getSensorsPollInterval() const {
      return _sensorsPollInterval;
    }

   protected:
    Flags _flags = Flags::None;
    Device(){};
    void init(const Flags flags);
    void handleEvent(Event &ev) override;
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
    virtual bool shouldPollSensors() {
      return millis() - _sensorsPolledAt > _sensorsPollInterval;
    };
    virtual unsigned long nextSensorsPollTime() {
      return _sensorsPolledAt + _sensorsPollInterval;
    };

   private:
    bool _sensorsReady = false;
    unsigned long _sensorsInitAt = 0;
    unsigned long _sensorsPolledAt = 0;
    unsigned int _reinitDelay = 10000;
    int _sensorsPollInterval = 1000;
    bool sensorsReady();
    static void setupSensorPollTask();
  };

  ENUM_FLAG_OPERATORS(Device::Flags)

  namespace sensor {

    enum class StateClass { Undefined, Measurement, Total, TotalIncreasing };

    Sensor *find(std::string uid);
    Sensor *find(Device *device, const char *id);
    int nextGroup();

    class Changed : public Event {
     public:
      Changed(const Changed &) = delete;
      const Sensor *sensor() const {
        return _sensor;
      }
      static void publish(const Sensor *sensor) {
        Changed ev(sensor);
        ev.Event::publish();
      }
      static bool is(Event &ev, Changed **changed) {
        if (ev.is(Type)) {
          if (changed)
            *changed = (Changed *)&ev;
          return true;
        }
        return false;
      }

     private:
      Changed(const Sensor *sensor) : Event(Type), _sensor(sensor) {}
      const Sensor *_sensor;
      constexpr static const char *Type = "sensor-changed";
    };

    class Group {
     public:
      Group(int id) : _id(id) {}
      struct Iterator {
       public:
        typedef std::map<std::string, Sensor *>::iterator Inner;
        Iterator(int id);
        bool operator==(const Iterator &other) const {
          return _inner == other._inner;
        }
        bool operator!=(const Iterator &other) const {
          return _inner != other._inner;
        }
        Sensor *operator*() const {
          return _inner->second;
        }
        Iterator &operator++() {
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

    class All {
     public:
      struct Iterator {
       public:
        typedef std::map<std::string, Sensor *>::iterator Inner;
        Iterator(Inner inner) : _inner(inner) {}
        bool operator==(const Iterator &other) const {
          return _inner == other._inner;
        }
        bool operator!=(const Iterator &other) const {
          return _inner != other._inner;
        }
        Sensor *operator*() const {
          return _inner->second;
        }
        Iterator &operator++() {
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

    class GroupChanged : public Event {
     public:
      GroupChanged(const GroupChanged &) = delete;
      const Group &group() const {
        return _group;
      }
      static void publish(int group) {
        GroupChanged ev(group);
        ev.Event::publish();
      }
      static bool is(Event &ev, GroupChanged **changed) {
        if (ev.is(Type)) {
          if (changed)
            *changed = (GroupChanged *)&ev;
          return true;
        }
        return false;
      }

     private:
      GroupChanged(int group) : Event(Type), _group(group) {}
      Group _group;
      constexpr static const char *Type = "sensor-group-changed";
    };

  }  // namespace sensor

  class Sensor {
   public:
    int precision = -1;
    int group = 0;
    bool disabled = false;
    sensor::StateClass stateClass = sensor::StateClass::Undefined;
    const char *unit = nullptr;
    const char *name = nullptr;
    Sensor(Device *device, const char *type, const char *id = nullptr,
           size_t size = 0);
    Sensor(const Sensor &) = delete;
    const char *type() const {
      return _type;
    }
    /**
     * @return sensor identifier that is unique in its device scope
     */
    const char *id() const {
      if (!_id.empty())
        return _id.c_str();
      return _type;
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
    Device *device() const {
      return _device;
    }
    bool is(const char *t) const {
      return t && !strcmp(type(), t);
    }
    template <typename T>
    void set(T value, bool *changed = nullptr) {
      if constexpr (std::is_same_v<T, float>)
        if (precision >= 0)
          value = roundTo(value, precision);
      auto c = _value != value;
      _value.set(value);
      if (c) {
        if (changed)
          *changed = true;
        if (group <= 0)
          sensor::Changed::publish(this);
      }
    }
    void to(JsonObject target) const {
      if (precision >= 0 && _value.is<float>())
        target[id()] = serialized(roundToString(_value.as<float>(), precision));
      else
        target[id()] = _value;
    }
    JsonVariantConst get() const {
      return _value;
    }

   private:
    Device *_device;
    const char *_type;
    std::string _id;
    DynamicJsonDocument _value;
  };

}  // namespace esp32m