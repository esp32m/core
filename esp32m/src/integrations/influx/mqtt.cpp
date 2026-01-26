#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "esp32m/integrations/influx/mqtt.hpp"
#include "esp32m/net/mqtt.hpp"

namespace esp32m {
using namespace dev;
  namespace integrations {
    namespace influx {

      // Escape special characters in tag values/field names for InfluxDB line protocol
      // Spaces, commas, and equals signs need to be escaped
      std::string escapeLineProtocolValue(const char* src) {
        std::string result;
        if (!src)
          return result;
        
        // Pre-allocate with some extra space for escapes (worst case: every char gets escaped)
        size_t srcLen = strlen(src);
        result.reserve(srcLen * 2);
        
        for (const char* p = src; *p; p++) {
          if (*p == '\\' || *p == ' ' || *p == ',' || *p == '=') {
            result += '\\';
          }
          result += *p;
        }
        return result;
      }

      std::string serializeProps(const JsonObjectConst props) {
        std::string result;
        if (!props)
          return result;

        // Rough pre-allocation: worst case escaping can double size
        result.reserve(measureJson(props) * 2);

        bool first = true;
        for (JsonPairConst kv : props) {
          if (!first)
            result += ',';
          first = false;

          result += escapeLineProtocolValue(kv.key().c_str());
          result += '=';

          if (kv.value().is<const char*>()) {
            result += escapeLineProtocolValue(kv.value().as<const char*>());
          } else {
            // Serialize non-string values and escape separators so they remain a single tag value.
            auto needed = measureJson(kv.value()) + 1;
            std::string tmp;
            tmp.resize(needed);
            // C++11-safe: std::string::data() may be const; &tmp[0] is writable when size() > 0
            auto written = serializeJson(kv.value(), &tmp[0], needed);
            tmp.resize(written);
            result += escapeLineProtocolValue(tmp.c_str());
          }
        }
        return result;
      }

      void Mqtt::handleEvent(Event& ev) {
        if (EventInit::is(ev, 0)) {
          auto name = App::instance().hostname();
          if (asprintf(&_sensorsTopic, "esp32m/sensor/%s", name) < 0)
            _sensorsTopic = nullptr;
        }
        dev::StateEmitter::handleEvent(ev);
      }

      void Mqtt::emit(std::vector<const dev::Component*> sensors) {
        if (!_sensorsTopic)
          return;
        std::string line;
        for (auto sensor : sensors) {
          if (!sensor)
            continue;
          auto value = sensor->getState();
          if (!(value.is<float>() ||
                ((sensor->isComponent(ComponentType::Switch) || sensor->isComponent(ComponentType::BinarySensor)) && value.is<bool>())))
            continue;
          auto unitName = App::instance().hostname();
          if (!unitName)
            unitName = "";
          auto device = sensor->device();
          if (!device)
            continue;
          auto devName = device->name();
          if (!devName)
            devName = "";
          auto name = sensor->type();
          if (!name || !strlen(name))
            name = "value";

          auto escapedUnitName = escapeLineProtocolValue(unitName);
          auto escapedDevName = escapeLineProtocolValue(devName);
          auto devProps = serializeProps(device->props());
          auto props = serializeProps(sensor->props());
          auto escapedTitle = sensor->title() ? escapeLineProtocolValue(sensor->title()) : std::string();
          auto escapedId = (sensor->id() && strcmp(sensor->id(), name) != 0) ? escapeLineProtocolValue(sensor->id()) : std::string();
          auto escapedFieldName = escapeLineProtocolValue(name);

          line.clear();
          line.reserve(
              6 /* "esp32m" */ +
              6 /* ",unit=" */ + escapedUnitName.size() +
              8 /* ",device=" */ + escapedDevName.size() +
              (devProps.empty() ? 0 : (1 /* comma */ + devProps.size())) +
              (props.empty() ? 0 : (1 /* comma */ + props.size())) +
              (escapedTitle.empty() ? 0 : (8 /* ",sensor=" */ + escapedTitle.size())) +
              (escapedId.empty() ? 0 : (4 /* ",id=" */ + escapedId.size())) +
              1 /* space */ + escapedFieldName.size() + 1 /* '=' */ +
              48 /* value buffer */);

          line += "esp32m,unit=";
          line += escapedUnitName;
          line += ",device=";
          line += escapedDevName;
          if (!devProps.empty()) {
            line += ',';
            line += devProps;
          }
          if (!props.empty()) {
            line += ',';
            line += props;
          }
          if (!escapedTitle.empty()) {
            line += ",sensor=";
            line += escapedTitle;
          }
          if (!escapedId.empty()) {
            line += ",id=";
            line += escapedId;
          }

          line += ' ';
          line += escapedFieldName;
          line += '=';

          char vbuf[48];
          if (value.is<float>())
            snprintf(vbuf, sizeof(vbuf), "%.16g", value.as<float>());
          else if (value.is<bool>())
            snprintf(vbuf, sizeof(vbuf), "%d", value.as<bool>() ? 1 : 0);
          else
            snprintf(vbuf, sizeof(vbuf), "%d", value.as<int>());
          line += vbuf;

          net::Mqtt::instance().publish(_sensorsTopic, line.c_str());
        }
      }
    }  // namespace influx
  }  // namespace integrations
}  // namespace esp32m
