#pragma once

#include "esp32m/app.hpp"
#include "esp32m/ha/ha.hpp"
#include "esp32m/net/mqtt.hpp"

#include <esp_task_wdt.h>

namespace esp32m {
  namespace ha {

    namespace mqtt {
      class Dev {
       public:
        std::string stateTopic;
        ~Dev() {
          delete _sub;
        }
        Dev(const char *name, std::string stateTopic, std::string commandTopic)
            : stateTopic(stateTopic), _name(name) {
          auto &mqtt = net::Mqtt::instance();
          _sub = mqtt.subscribe(commandTopic.c_str(),
                                [this](std::string topic, std::string payload) {
                                  command(payload);
                                });
        }
        const char *name() const {
          return _name;
        }

       private:
        const char *_name;
        net::mqtt::Subscription *_sub;
        void command(std::string payload) {
          logi("received MQTT command: %s", payload.c_str());
        }
      };
    }  // namespace mqtt

    class Mqtt : public AppObject {
     public:
      const char *name() const override {
        return "ha-mqtt";
      };
      static Mqtt &instance() {
        static Mqtt i;
        return i;
      }

     protected:
      void handleEvent(Event &ev) override {
        if (EventInited::is(ev)) {
          xTaskCreate([](void *self) { ((Mqtt *)self)->run(); }, "m/ha-mqtt",
                      4096, this, tskIDLE_PRIORITY, &_task);
        }
      }

     private:
      TaskHandle_t _task = nullptr;
      unsigned long _describeRequested = 0, _stateRequested = 0;
      std::map<std::string, std::unique_ptr<mqtt::Dev> > _devices;
      Mqtt(){};
      void run() {
        esp_task_wdt_add(NULL);
        auto &mqtt = net::Mqtt::instance();
        for (;;) {
          esp_task_wdt_reset();
          ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
          if (!mqtt.isReady())
            continue;

          auto curtime = millis();
          if (_describeRequested == 0 || (curtime - _describeRequested > 60000))
            describe();
          if (_stateRequested == 0 || (curtime - _stateRequested > 5000))
            requestState();
        }
      }
      void describe() {
        _describeRequested = millis();
        DescribeRequest req(nullptr);
        req.publish();
        auto &mqtt = net::Mqtt::instance();
        auto nodeid = App::instance().hostname();
        for (auto const &[id, doc] : req.responses) {
          JsonVariantConst data = doc->as<JsonVariantConst>();
          auto source = data["name"].as<const char *>();
          auto component = data["component"].as<const char *>();
          if (!component || !strlen(component)) {
            logW("device %s did not provide HA component name", id.c_str());
            continue;
          }
          std::string configTopic = string_printf(
              "homeassistant/%s/%s/%s/config", component, nodeid, id.c_str());
          auto configInput = data["config"].as<JsonObjectConst>();
          if (!configInput) {
            logW("device %s did not provide HA config ", id.c_str());
            continue;
          }

          std::string stateTopic = string_printf("homeassistant/%s/%s/%s/state",
                                                 component, nodeid, id.c_str());
          DynamicJsonDocument *configPayloadDoc = new DynamicJsonDocument(
              configInput.memoryUsage() + JSON_OBJECT_SIZE(1) +
              JSON_STRING_SIZE(stateTopic.size()));
          configPayloadDoc->set(configInput);
          auto configPayloadRoot = configPayloadDoc->as<JsonObject>();
          configPayloadRoot["state_topic"] = stateTopic;

          auto configPayload =
              json::allocSerialize(configPayloadDoc->as<JsonVariantConst>());
          delete configPayloadDoc;

          // logI("config topic: %s, payload: %s", configTopic.c_str(),
          // configPayload);
          mqtt.publish(configTopic.c_str(), configPayload);
          free(configPayload);

          auto it = _devices.find(id);
          if (it == _devices.end()) {
            std::string commandTopic =
                string_printf("homeassistant/%s/%s/%s/command", component,
                              nodeid, id.c_str());
            _devices[id] = std::unique_ptr<mqtt::Dev>(
                new mqtt::Dev(source, stateTopic, commandTopic));
          }
        }
      }
      void requestState() {
        _stateRequested = millis();
        StaticJsonDocument<JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(64)> reqDoc;
        auto reqData = reqDoc.to<JsonObject>();
        auto &mqtt = net::Mqtt::instance();
        for (auto const &[id, dev] : _devices) {
          reqData["id"] = id;
          StateRequest req(dev->name(), reqData);
          req.publish();
          if (!req.response) {
            logW("no response to state request from device %s", id.c_str());
            continue;
          }
          JsonVariantConst data = req.response->as<JsonVariantConst>();
          auto state = data["state"].as<JsonVariantConst>();
          if (!state) {
            logW("device %s did not provide state", id.c_str());
            continue;
          }
          auto statePayload = json::allocSerialize(state);
          // logI("state topic: %s, payload: %s", dev->stateTopic.c_str(),
          // statePayload);
          mqtt.publish(dev->stateTopic.c_str(), statePayload);
          free(statePayload);
        }
      }
    };

    Mqtt *useMqtt() {
      return &Mqtt::instance();
    }
  }  // namespace ha
}  // namespace esp32m