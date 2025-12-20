#include <ArduinoJson.h>

#include "esp32m/integrations/influx/mqtt.hpp"
#include "esp32m/net/mqtt.hpp"

namespace esp32m {
  namespace integrations {
    namespace influx {

      size_t serializeProps(const JsonObjectConst props, char** buf) {
        if (props) {
          auto size = measureJson(props);
          char* bptr = *buf = (char*)malloc(size);

          for (JsonPairConst kv : props) {
            if (!size)
              break;
            if (bptr != *buf) {
              *(++bptr) = ',';
              size--;
            }
            auto l = snprintf(bptr, size, "%s=", kv.key().c_str());
            bptr += l;
            size -= l;
            if (!size)
              break;
            if (kv.value().is<const char*>())
              l = strlcpy(bptr, kv.value().as<const char*>(), size);
            else
              l = serializeJson(kv.value(), bptr, size);
            bptr += l;
            size -= l;
          }
          return bptr - *buf;
        }

        return 0;
      }

      void Mqtt::handleEvent(Event& ev) {
        if (EventInit::is(ev, 0)) {
          auto name = App::instance().hostname();
          if (asprintf(&_sensorsTopic, "esp32m/sensor/%s", name) < 0)
            _sensorsTopic = nullptr;
        }
        sensor::StateEmitter::handleEvent(ev);
      }

      void Mqtt::emit(std::vector<const Sensor*> sensors) {
        for (auto sensor : sensors) {
          auto value = sensor->get();
          if (!(value.is<float>() ||
                ((sensor->isComponent(ComponentType::Switch) || sensor->isComponent(ComponentType::BinarySensor)) && value.is<bool>())))
            continue;
          char *devProps, *props;
          auto unitName = App::instance().hostname();
          auto device = sensor->device();
          auto devName = device->name();
          auto dpl = serializeProps(device->props(), &devProps);
          auto pl = serializeProps(sensor->props(), &props);
          auto name = sensor->type();
          if (!name || !strlen(name))
            name = "value";
          auto unl = strlen(unitName);
          auto dnl = strlen(devName);
          auto snl = sensor->name ? strlen(sensor->name) : 0;
          auto idl= sensor->id()!=name ? strlen(sensor->id()) : 0;
          auto dl = 12 /* "esp32m,unit=" */ + unl + 8 /* ",device=" */ + dnl +
                    (dpl ? (1 /* comma */ + dpl) : 0) +
                    (pl ? (1 /* comma */ + pl) : 0) +
                    (snl ? (8 /* ",sensor=" */ + snl) : 0) + 1 /*space*/ +
                    (idl ? (4 /* ",id=" */ + idl) : 0) + 1 /*space*/ +
                    strlen(name) + 1 /*=*/ + (16 + 1 /*value*/) + 1 /*null*/;
          char* s = (char*)malloc(dl);
          char* sp = s;
          auto l =
              snprintf(sp, dl, "esp32m,unit=%s,device=%s", unitName, devName);
          sp += l;
          dl -= l;
          if (dpl && dl) {
            l = snprintf(sp, dl, ",%s", devProps);
            sp += l;
            dl -= l;
          }
          if (pl && dl) {
            l = snprintf(sp, dl, ",%s", props);
            sp += l;
            dl -= l;
          }
          if (snl && dl) {
            l = snprintf(sp, dl, ",sensor=%s", sensor->name);
            sp += l;
            dl -= l;
          }
          if (idl && dl) {
            l = snprintf(sp, dl, ",id=%s", sensor->id());
            sp += l;
            dl -= l;
          }
          if (dl) {
            if (value.is<float>())
              l = snprintf(sp, dl, " %s=%.16g", name, value.as<float>());
            else if (value.is<bool>())
              l = snprintf(sp, dl, " %s=%d", name, value.as<bool>() ? 1 : 0);
            else
              l = snprintf(sp, dl, " %s=%d", name, value.as<int>());
            sp += l;
            dl -= l;
          }
          if (dpl)
            free(devProps);
          if (pl)
            free(props);
          net::Mqtt::instance().publish(_sensorsTopic, s);
          free(s);
        }
      }
    }  // namespace influx
  }  // namespace integrations
}  // namespace esp32m
