#pragma once

#include "ArduinoJson.h"
#include "esp32m/logging.hpp"
#include "esp32m/net/net.hpp"

#include <esp_mac.h>
#include <hal/efuse_hal.h>

namespace esp32m {
  namespace ha {

    class ConfigBuilder : public log::Loggable {
     public:
      std::string configTopic, configPayload, stateTopic, commandTopic;
      const char* name() const override {
        return _id;
      }
      ConfigBuilder(const char* id, JsonVariantConst descriptor)
          : _id(id), _descriptor(descriptor) {}

      bool build() {
        auto component = _descriptor["component"].as<const char*>();
        if (!component || !strlen(component)) {
          logW("device did not provide HA component name");
          return false;
        }
        _component = component;
        _descriptorConfig = _descriptor["config"];
        if (!_descriptorConfig) {
          logW("device did not provide HA config");
          return false;
        }
        inferUniqueId();
        inferName();
        inferDeviceInfo();
        inferStateTopic();
        inferCommandTopic();
        inferAvailability();

        buildConfigPayload();
        configTopic = string_printf("homeassistant/%s/%s/config", component,
                                    _uniqueId.c_str());
        return true;
      }

     private:
      const char* _id;
      JsonVariantConst _descriptor;
      JsonObjectConst _descriptorConfig;
      std::vector<const char*> _stateTopicNames, _commandTopicNames;
      std::string _component, _uniqueId, _name, _availabilityTopic,
          _payloadAvailable, _payloadUnavailable;
      std::map<std::string, std::string> _deviceProps;
      bool _addUniqueId = false, _addName = false;
      size_t _configPayloadSize = 0;
      void inferUniqueId() {
        auto uniqueId = _descriptorConfig["unique_id"].as<const char*>();
        if (uniqueId)
          _uniqueId = uniqueId;
        if (_uniqueId.empty()) {
          _uniqueId = string_printf("%s_%s", App::instance().hostname(), _id);
          _addUniqueId = true;
          _configPayloadSize +=
              JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(_uniqueId.size());
        }
      }
      void inferName() {
        auto name = _descriptorConfig["name"].as<const char*>();
        if (name)
          _name = name;
        if (_name.empty()) {
          _name = _id;
          _addName = true;
          _configPayloadSize +=
              JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(_name.size());
        }
      }
      void inferDeviceProp(JsonObjectConst device, const char* name) {
        auto& props = App::instance().props();
        auto prop = props.get(name);
        if (!device[name] && !prop.empty()) {
          _deviceProps[name] = prop;
          _configPayloadSize +=
              JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(prop.size());
        }
      }
      void inferDeviceInfo() {
        JsonObjectConst device = _descriptorConfig["device"];
        if (!device) {
          _configPayloadSize += JSON_OBJECT_SIZE(1);
        }
        if (!device["connections"])
          _configPayloadSize += JSON_OBJECT_SIZE(1);
        _configPayloadSize +=
            JSON_ARRAY_SIZE(2 + 2 * 2) +
            JSON_STRING_SIZE(
                net::MacMaxChars);  // [["mac", mac],["hostname", hostname]]

        if (!device["name"]) {
          std::string name = App::instance().name();
          _deviceProps["name"] = name;
          _configPayloadSize +=
              JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(name.size());
        }
        if (!device["configuration_url"]) {
          auto url =
              string_printf("http://%s.local/", App::instance().hostname());
          _deviceProps["configuration_url"] = url;
          _configPayloadSize +=
              JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(url.size());
        }
        inferDeviceProp(device, "model");
        inferDeviceProp(device, "manufacturer");
        inferDeviceProp(device, "hw_version");
        inferDeviceProp(device, "sw_version");
      }
      void inferCommandTopic() {
        JsonArrayConst commandTopicNames = _descriptor["commandTopicNames"];
        if (commandTopicNames.size())
          for (const char* name : commandTopicNames)
            _commandTopicNames.push_back(name);
        if (!_commandTopicNames.size())
          if (_component == "switch")
            _commandTopicNames.push_back("command_topic");

        if (_commandTopicNames.size()) {
          commandTopic = _descriptor["commandTopic"] |
                         string_printf("esp32m/%s/command", _uniqueId.c_str());
          _configPayloadSize +=
              JSON_OBJECT_SIZE(_commandTopicNames.size()) +
              JSON_STRING_SIZE(commandTopic.size()) * _commandTopicNames.size();
        }
      }
      void inferStateTopic() {
        JsonArrayConst stateTopicNames = _descriptor["stateTopicNames"];
        if (stateTopicNames.size())
          for (const char* name : stateTopicNames)
            _stateTopicNames.push_back(name);
        if (!_stateTopicNames.size())
          if (_component == "sensor" || _component == "switch")
            _stateTopicNames.push_back("state_topic");
        if (_stateTopicNames.size()) {
          stateTopic = _descriptor["stateTopic"] |
                       string_printf("esp32m/%s/state", _uniqueId.c_str());
          _configPayloadSize +=
              JSON_OBJECT_SIZE(_stateTopicNames.size()) +
              JSON_STRING_SIZE(stateTopic.size()) * _stateTopicNames.size();
        }
      }
      void inferAvailability() {
        if (_descriptorConfig["availability_topic"])
          return;
        auto& mqtt = net::Mqtt::instance();
        auto& lwt = mqtt.getLwt();
        auto& birth = mqtt.getBirth();
        if (lwt.valid() && birth.valid() && lwt.topic == birth.topic) {
          _configPayloadSize += JSON_OBJECT_SIZE(3) +
                                JSON_STRING_SIZE(birth.topic.size()) +
                                JSON_STRING_SIZE(birth.payload.size()) +
                                JSON_STRING_SIZE(lwt.payload.size());
          _availabilityTopic = birth.topic;
          _payloadAvailable = birth.payload;
          _payloadUnavailable = lwt.payload;
        }
      }
      void buildConfigPayload() {
        uint8_t mac[6];
        efuse_hal_get_mac(mac);
        char macbuf[net::MacMaxChars];
        sprintf(macbuf, MACSTR, MAC2STR(mac));

        auto doc = new DynamicJsonDocument(_descriptorConfig.memoryUsage() +
                                           _configPayloadSize);

        doc->set(_descriptorConfig);
        auto root = doc->as<JsonObject>();
        if (_addName)
          root["name"] = _name;
        if (_addUniqueId)
          root["unique_id"] = _uniqueId;
        if (!_availabilityTopic.empty())
          root["availability_topic"] = _availabilityTopic;
        if (!_payloadAvailable.empty())
          root["payload_available"] = _payloadAvailable;
        if (!_payloadUnavailable.empty())
          root["payload_not_available"] = _payloadUnavailable;

        JsonObject device = root["device"] | root.createNestedObject("device");
        JsonArray connections =
            device["connections"] | device.createNestedArray("connections");

        JsonArray mc = connections.createNestedArray();
        mc.add("mac");
        mc.add(macbuf);
        mc = connections.createNestedArray();
        mc.add("hostname");
        mc.add(App::instance().hostname());

        if (_deviceProps.size())
          for (auto const& [key, value] : _deviceProps) device[key.c_str()] = value;

        if (_stateTopicNames.size())
          for (const char* name : _stateTopicNames) root[name] = stateTopic;

        if (_commandTopicNames.size())
          for (const char* name : _commandTopicNames) root[name] = commandTopic;

        json::check(this, doc, "HA config payload");
        configPayload = json::serialize(root);
        delete doc;
      }
    };

  }  // namespace ha

}  // namespace esp32m
