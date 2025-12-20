#pragma once

#include <unordered_set>

#include "esp32m/app.hpp"
#include "esp32m/integrations/ha/config.hpp"
#include "esp32m/integrations/ha/ha.hpp"
#include "esp32m/net/mqtt.hpp"

#include <esp_task_wdt.h>

namespace esp32m {
  namespace integrations {
    namespace ha {

      namespace mqtt {

        static inline void wakeUp();

        class Dev {
         public:
          std::string stateTopic;
          ~Dev() {
            if (_sub)
              delete _sub;
          }
          Dev(std::string name, std::string stateTopic,
              std::string commandTopic)
              : stateTopic(stateTopic), _name(name) {
            if (!commandTopic.empty()) {
              auto& mqtt = net::Mqtt::instance();
              _sub = mqtt.subscribe(
                  commandTopic.c_str(),
                  [this](std::string topic, std::string payload) {
                    command(payload);
                  });
            }
          }
          const char* name() const {
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
          net::mqtt::Subscription* _sub = nullptr;
          bool _stateChanged = false;
          void command(std::string payload) {
            auto doc =
                new JsonDocument(); /* JSON_STRING_SIZE(payload.size()) */
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
        const char* name() const override {
          return "ha-mqtt";
        };
        static Mqtt& instance() {
          static Mqtt i;
          return i;
        }
        void wakeUp() {
          if (_task)
            xTaskNotifyGive(_task);
        }

       protected:
        void handleEvent(Event& ev) override {
          // EventStateChanged *evch;
          if (EventInited::is(ev)) {
            xTaskCreate([](void* self) { ((Mqtt*)self)->run(); }, "m/ha-mqtt",
                        4096, this, tskIDLE_PRIORITY, &_task);
          }
        }

       private:
        TaskHandle_t _task = nullptr;
        unsigned long _describeRequested = 0, _stateRequested = 0;
        net::mqtt::Subscription* _configSub = nullptr;
        std::map<std::string, std::unique_ptr<mqtt::Dev> > _devices;
        static constexpr const int FlagConfigPublished = 1;
        static constexpr const int FlagConfigValid = 2;
        bool _checkInvalidConfigs = false;
        std::mutex _publishedConfigsMutex;
        std::map<std::string, int> _publishedConfigs;
        Mqtt() {};
        void run() {
          esp_task_wdt_add(NULL);
          auto& mqtt = net::Mqtt::instance();
          for (;;) {
            esp_task_wdt_reset();
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)))
              requestState(true);
            if (!mqtt.isReady())
              continue;
            if (!_configSub) {
              _configSub = mqtt.subscribe(
                  "homeassistant/+/+/config",
                  [this](std::string topic, std::string payload) {
                    // Split by '/' and extract third segment
                    // Format: homeassistant/segment1/segment2/config
                    if (payload.empty())
                      return; // ignore empty payloads - they are deletions
                    size_t pos1 = topic.find('/');  // after "homeassistant"
                    if (pos1 == std::string::npos)
                      return;

                    size_t pos2 = topic.find('/', pos1 + 1);  // after segment1
                    if (pos2 == std::string::npos)
                      return;

                    size_t pos3 = topic.find('/', pos2 + 1);  // after segment2
                    if (pos3 == std::string::npos)
                      return;

                    // Extract segment2 (third segment)
                    std::string segment =
                        topic.substr(pos2 + 1, pos3 - pos2 - 1);
                    std::string hostMatch =
                        std::string(App::instance().hostname()) + "_";
                    // Check if it starts with known hostname
                    if (segment.find(hostMatch) == 0) {
                      // Process the config message
                      std::lock_guard<std::mutex> lock(_publishedConfigsMutex);
                      _publishedConfigs[topic] |= FlagConfigPublished;
                      _checkInvalidConfigs = true;
                    }
                  },
                  1);
            }
            auto curtime = millis();
            if (_describeRequested == 0 ||
                (curtime - _describeRequested > 24 * 60 * 60 * 1000))
              describe();
            checkInvalidConfigs();
            /*if (_stateRequested == 0 || (curtime - _stateRequested > 60 *
              1000)) requestState(false);*/
          }
        }

        void checkInvalidConfigs() {
          auto& mqtt = net::Mqtt::instance();
          std::lock_guard<std::mutex> lock(_publishedConfigsMutex);
          if (!_checkInvalidConfigs)
            return;
          _checkInvalidConfigs = false;
          for (auto it = _publishedConfigs.begin();
               it != _publishedConfigs.end();) {
            // logD("Config message: %s: %d", it->first.c_str(), it->second);
            if (it->second == FlagConfigPublished) {
              logD("Removing config message: %s", it->first.c_str());
              mqtt.publish(it->first.c_str(), "", 1, true);
              it = _publishedConfigs.erase(it);
            } else {
              ++it;
            }
          }
        }

        void describe() {
          _describeRequested = millis();
          DescribeRequest req(nullptr);
          req.publish();
          for (auto const& [key, doc] : req.responses) {
            auto data = doc->as<JsonVariantConst>();
            const char* id = data["id"] | key.c_str();
            publishConfig(id, data);
          }
          sensor::All sensors;
          for (auto sensor : sensors)
            if (!sensor->disabled) {
              auto id = sensor->uid();
              auto it = req.responses.find(id);
              if (it == req.responses.end()) {
                auto doc = describeSensor(sensor);
                json::check(this, doc, "describeSensor");
                auto data = doc->as<JsonVariantConst>();
                publishConfig(id.c_str(), data);
                delete doc;
              }
            }
        }

        void publishConfig(const char* id, JsonVariantConst data) {
          ConfigBuilder builder(id, data);
          if (builder.build()) {
            auto& mqtt = net::Mqtt::instance();
            /*logD("config topic: %s, payload: %s, acceptsCommands=%d",
                 configTopic.c_str(), configPayload, acceptsCommands);*/
            mqtt.publish(builder.configTopic.c_str(),
                         builder.configPayload.c_str(), 1, true);

            auto it = _devices.find(id);
            if (it == _devices.end()) {
              _devices[id] = std::unique_ptr<mqtt::Dev>(new mqtt::Dev(
                  data["name"], builder.stateTopic, builder.commandTopic));
            }
            std::lock_guard<std::mutex> lock(_publishedConfigsMutex);
            _publishedConfigs[builder.configTopic] |= FlagConfigValid;
          }
        }

        void requestState(bool changedOnly) {
          const size_t MaxStaticIdLength = 32;
          _stateRequested = millis();
          /*StaticJsonDocument<JSON_OBJECT_SIZE(1) +
                             JSON_STRING_SIZE(MaxStaticIdLength)>*/
          JsonDocument reqDoc;
          auto reqData = reqDoc.to<JsonObject>();
          auto& mqtt = net::Mqtt::instance();
          for (auto const& [id, dev] : _devices) {
            if (changedOnly && !dev->isStateChanged())
              continue;
            dev->resetStateChanged();
            JsonDocument* dynReqDoc = nullptr;
            JsonObject safeReqData = reqData;
            if (id.size() > MaxStaticIdLength) {  // very unlikely, but still
                                                  // need to account for it
              dynReqDoc = new JsonDocument(/*JSON_OBJECT_SIZE(1) +
                                           JSON_STRING_SIZE(id.size())*/);
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
            /*char *statePayload;
            bool jsonStatePayload = false;
            if (state.is<const char *>()) {
              statePayload = (char *)state.as<const char *>();
            } else {
              statePayload = json::allocSerialize(state);
              jsonStatePayload = true;
            }*/
            std::string statePayload;
            if (state.is<const char*>()) {
              statePayload = state.as<const char*>();
            } else {
              serializeJson(state, statePayload);
            }
            /*logD("state topic: %s, payload: %s", dev->stateTopic.c_str(),
                 statePayload);*/
            mqtt.publish(dev->stateTopic.c_str(), statePayload.c_str());
            /*if (jsonStatePayload)
              free(statePayload);*/
          }
        }
      };

      static inline Mqtt* useMqtt() {
        return &Mqtt::instance();
      }

      namespace mqtt {
        static inline void wakeUp() {
          ha::Mqtt::instance().wakeUp();
        }
      }  // namespace mqtt
    }  // namespace ha
  }  // namespace integrations
}  // namespace esp32m