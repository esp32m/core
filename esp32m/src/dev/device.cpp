#include "esp32m/device.hpp"
#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/net/ota.hpp"
#include "esp32m/sleep.hpp"

#include <esp_task_wdt.h>
#include <math.h>

namespace esp32m {
  const char *EventSensor::NAME = "sensor";

  class EventPollSensors : public Event {
   public:
    EventPollSensors() : Event(NAME) {}
    static bool is(Event &ev) {
      return ev.is(NAME);
    }

   private:
    static const char *NAME;
  };

  const char *EventPollSensors::NAME = "poll-sensors";

  Device::Device(Flags flags) {
    if (flags & Flags::HasSensors) {
      setupSensorPollTask();
      EventManager::instance().subscribe([this](Event &ev) {
        if (EventPollSensors::is(ev) && sensorsReady())
          if (!pollSensors())
            resetSensors();
      });
    }
  }

  void Device::sensor(const char *sensor, const float value) {
    if (!isnan(value))
      EventSensor::publish(*this, sensor, value, json::null<JsonObjectConst>());
  };

  void Device::sensor(const char *sensor, const float value,
                      const JsonObjectConst props) {
    if (!isnan(value))
      EventSensor::publish(*this, sensor, value, props);
  };

  void Device::setupSensorPollTask() {
    static TaskHandle_t task = nullptr;
    static Sleeper sleeper;
    static EventPollSensors ev;
    if (!task)
      xTaskCreate(
          [](void *) {
            EventManager::instance().subscribe([](Event &ev) {
              if (EventInited::is(ev) && task)
                xTaskNotifyGive(task);
            });
            esp_task_wdt_add(NULL);
            for (;;) {
              esp_task_wdt_reset();
              if (App::initialized()) {
                {
                  locks::Guard guard(net::ota::Name);
                  ev.publish();
                }
                sleeper.sleep();
              } else
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            }
          },
          "m/sensors", 4096, nullptr, 1, &task);
  }

  bool Device::sensorsReady() {
    if (_sensorsReady)
      return true;
    unsigned int millis = esp_timer_get_time() / 1000;
    if (_sensorsInitAt && millis - _sensorsInitAt < _reinitDelay)
      return false;
    _sensorsInitAt = millis;
    _sensorsReady = initSensors();
    if (_sensorsReady)
      logI("device initialized");
    return _sensorsReady;
  }

}  // namespace esp32m
