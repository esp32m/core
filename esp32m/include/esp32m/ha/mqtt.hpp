#pragma once

#include "esp32m/app.hpp"
#include "esp32m/ha/config.hpp"
#include "esp32m/ha/ha.hpp"
#include "esp32m/net/mqtt.hpp"

#include <esp_task_wdt.h>

namespace esp32m {
  namespace ha {

    namespace mqtt {

      void wakeUp();

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
        bool isStateChanged() {
          return _stateChanged;
        }
        void resetStateChanged() {
          _stateChanged = false;
        }
        void setStateChanged() {
          _stateChanged = true;
          wakeUp();
        }

       private:
        std::string _name;
        net::mqtt::Subscription *_sub = nullptr;
        bool _stateChanged = false;
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
      void wakeUp() {
        if (_task)
          xTaskNotifyGive(_task);
      }

     protected:
      void handleEvent(Event &ev) override {
        if (EventInited::is(ev)) {
          xTaskCreate([](void *self) { ((Mqtt *)self)->run(); }, "m/ha-mqtt",
                      4096, this, tskIDLE_PRIORITY, &_task);
        } else if (EventStateChanged::is(ev)) {
          auto &evch = (EventStateChanged &)ev;
          auto name = evch.id();
          if (!name)
            name = evch.object()->name();
          // logI("state changed %s", name);
          auto it = _devices.find(name);
          if (it != _devices.end())
            it->second->setStateChanged();
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
          if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)))
            requestState(true);
          if (!mqtt.isReady())
            continue;

          auto curtime = millis();
          if (_describeRequested == 0 ||
              (curtime - _describeRequested > 24 * 60 * 60 * 1000))
            describe();
          /*if (_stateRequested == 0 || (curtime - _stateRequested > 60 * 1000))
            requestState(false);*/
        }
      }
      void describe() {
        _describeRequested = millis();
        DescribeRequest req(nullptr);
        req.publish();
        auto &mqtt = net::Mqtt::instance();
        for (auto const &[key, doc] : req.responses) {
          auto data = doc->as<JsonVariantConst>();
          const char *id = data["id"] | key.c_str();
          ConfigBuilder builder(id, data);
          if (builder.build()) {
            /*logD("config topic: %s, payload: %s, acceptsCommands=%d",
                 configTopic.c_str(), configPayload, acceptsCommands);*/
            mqtt.publish(builder.configTopic.c_str(),
                         builder.configPayload.c_str());

            auto it = _devices.find(id);
            if (it == _devices.end()) {
              _devices[id] = std::unique_ptr<mqtt::Dev>(new mqtt::Dev(
                  data["name"], builder.stateTopic, builder.commandTopic));
            }
          }
        }
      }
      void requestState(bool changedOnly) {
        const size_t MaxStaticIdLength = 32;
        _stateRequested = millis();
        StaticJsonDocument<JSON_OBJECT_SIZE(1) +
                           JSON_STRING_SIZE(MaxStaticIdLength)>
            reqDoc;
        auto reqData = reqDoc.to<JsonObject>();
        auto &mqtt = net::Mqtt::instance();
        for (auto const &[id, dev] : _devices) {
          if (changedOnly && !dev->isStateChanged())
            continue;
          dev->resetStateChanged();
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
          /*logD("state topic: %s, payload: %s", dev->stateTopic.c_str(),
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

    namespace mqtt {
      void wakeUp() {
        ha::Mqtt::instance().wakeUp();
      }
    }  // namespace mqtt
  }    // namespace ha
}  // namespace esp32m