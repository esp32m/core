#pragma once

#include <ArduinoJson.h>

#include "esp32m/app.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/events.hpp"
#include "esp32m/json.hpp"

namespace esp32m {

  class Device;

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

    /* these must be called only from within pollSensors() */
    void sensor(const char *sensor, const float value);
    void sensor(const char *sensor, const float value,
                const JsonObjectConst props);

   protected:
    Flags _flags;
    Device(const Flags flags = Flags::None);
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

   private:
    bool _sensorsReady = false;
    unsigned long _sensorsInitAt = 0;
    unsigned int _reinitDelay = 10000;
    bool sensorsReady();
    static void setupSensorPollTask();
  };

  ENUM_FLAG_OPERATORS(Device::Flags)

}  // namespace esp32m