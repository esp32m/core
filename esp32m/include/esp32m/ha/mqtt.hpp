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
          if (_sub)
            delete _sub;
        }
        Dev(std::string name, std::string stateTopic, std::string commandTopic)
            : stateTopic(stateTopic), _name(name) {
          if (!commandTopic.empty()) {
            auto &mqtt = net::Mqtt::instance();
            _sub =
                mqtt.subscribe(commandTopic.c_str(),
                               [this](std::string topic, std::string payload) {
                                 command(payload);
                               });
          }
        }
        const char *name() const {
          return _name.c_str();
        }

       private:
        std::string _name;
        net::mqtt::Subscription *_sub = nullptr;
        void command(std::string payload) {
          auto doc = new DynamicJsonDocument(JSON_STRING_SIZE(payload.size()));
          auto root = doc->to<JsonVariant>();
          root.set(payload);
          CommandRequest ev(name(), root);
          ev.publish();
          delete doc;
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
      std::string _dtp = "homeassistant";
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
        auto dtp = _dtp.c_str();
        for (auto const &[key, doc] : req.responses) {
          auto data = doc->as<JsonVariantConst>();
          const char *id = data["id"] | key.c_str();
          auto component = data["component"].as<const char *>();
          if (!component || !strlen(component)) {
            logW("device %s did not provide HA component name", id);
            continue;
          }
          auto configInput = data["config"].as<JsonObjectConst>();
          if (!configInput) {
            logW("device %s did not provide HA config ", id);
            continue;
          }
          auto configTopic =
              string_printf("%s/%s/%s/%s/config", dtp, component, nodeid, id);
          auto stateTopic =
              string_printf("%s/%s/%s/%s/state", dtp, component, nodeid, id);
          std::string commandTopic;
          bool acceptsCommands = DescribeRequest::getAcceptsCommands(data);
          if (acceptsCommands) {
            commandTopic = string_printf("%s/%s/%s/%s/command", dtp, component,
                                         nodeid, id);
          }
          bool addName = !configInput.containsKey("name");
          auto configPayloadDoc = new DynamicJsonDocument(
              configInput.memoryUsage() +
              JSON_OBJECT_SIZE(1 + (acceptsCommands ? 1 : 0) +
                               (addName ? 1 : 0)) +
              JSON_STRING_SIZE(stateTopic.size()) +
              JSON_STRING_SIZE(commandTopic.size()));
          configPayloadDoc->set(configInput);
          auto configPayloadRoot = configPayloadDoc->as<JsonObject>();
          if (addName)
            configPayloadRoot["name"] = id;
          configPayloadRoot["state_topic"] = stateTopic;
          if (acceptsCommands)
            configPayloadRoot["command_topic"] = commandTopic;
          json::check(this, configPayloadDoc, "HA config payload");
          auto configPayload =
              json::allocSerialize(configPayloadDoc->as<JsonVariantConst>());
          delete configPayloadDoc;

          /*logI("config topic: %s, payload: %s, acceptsCommands=%d",
               configTopic.c_str(), configPayload, acceptsCommands);*/
          mqtt.publish(configTopic.c_str(), configPayload);
          free(configPayload);

          auto it = _devices.find(id);
          if (it == _devices.end()) {
            _devices[id] = std::unique_ptr<mqtt::Dev>(
                new mqtt::Dev(data["name"], stateTopic, commandTopic));
          }
        }
      }
      void requestState() {
        const size_t MaxStaticIdLength = 32;
        _stateRequested = millis();
        StaticJsonDocument<JSON_OBJECT_SIZE(1) +
                           JSON_STRING_SIZE(MaxStaticIdLength)>
            reqDoc;
        auto reqData = reqDoc.to<JsonObject>();
        auto &mqtt = net::Mqtt::instance();
        for (auto const &[id, dev] : _devices) {
          DynamicJsonDocument *dynReqDoc = nullptr;
          JsonObject safeReqData = reqData;
          if (id.size() > MaxStaticIdLength) {  // very unlikely, but still need
                                                // to account for it
            dynReqDoc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1) +
                                                JSON_STRING_SIZE(id.size()));
            safeReqData = dynReqDoc->to<JsonObject>();
          }
          safeReqData["id"] = id;
          StateRequest req(dev->name(), safeReqData);
          req.publish();
          if (dynReqDoc)
            delete dynReqDoc;
          if (!req.response) {
            logW("no response to state request from device %s (%s)",
                 dev->name(), id.c_str());
            continue;
          }
          JsonVariantConst data = req.response->as<JsonVariantConst>();
          auto state = data["state"].as<JsonVariantConst>();
          if (!state) {
            logW("device %s did not provide state", id.c_str());
            continue;
          }
          char *statePayload;
          bool jsonStatePayload = false;
          if (state.is<const char *>()) {
            statePayload = (char *)state.as<const char *>();
          } else {
            statePayload = json::allocSerialize(state);
            jsonStatePayload = true;
          }
          /*logI("state topic: %s, payload: %s", dev->stateTopic.c_str(),
               statePayload);*/
          mqtt.publish(dev->stateTopic.c_str(), statePayload);
          if (jsonStatePayload)
            free(statePayload);
        }
      }
    };

    Mqtt *useMqtt() {
      return &Mqtt::instance();
    }
  }  // namespace ha
}  // namespace esp32m