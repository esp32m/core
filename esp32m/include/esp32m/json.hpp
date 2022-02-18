#pragma once

#include <esp_netif.h>
#include <lwip/ip_addr.h>
#include <memory>

#include <ArduinoJson.h>

#include "logging.hpp"

namespace esp32m {

  namespace json {

    DynamicJsonDocument *parse(const char *data, int len = -1);
    char *allocSerialize(const JsonVariantConst v);
    size_t measure(const JsonVariantConst v);

    void checkSetResult(esp_err_t err, DynamicJsonDocument **result);

    JsonDocument &empty();

    template <class T>
    T null() {
      return empty().as<T>();
    }

    template <typename T>
    void set(T &target, JsonVariantConst v, bool *changed = nullptr) {
      if (v.isUnbound() || v.isNull() || v.as<T>() == target)
        return;
      if (changed)
        *changed = true;
      target = v.as<T>();
    }

#ifdef ARDUINO
    void compareSet(IPAddress &target, JsonVariantConst v, bool &changed);
#endif

    void compareSet(ip_addr_t &target, JsonVariantConst v, bool &changed);
    void compareSet(ip4_addr_t &target, JsonVariantConst v, bool &changed);
    void compareSet(esp_ip4_addr_t &target, JsonVariantConst v, bool &changed);

    template <typename T>
    void compareSet(T &target, JsonVariantConst v, bool &changed) {
      if (v.isUnbound() || v.isNull() || v == target)
        return;
      changed = true;
      target = v.as<T>();
    }

    template <typename T>
    void set(T &target, JsonVariantConst v, T def) {
      if (!v.isUnbound())
        target = v.as<T>();
      else
        target = def;
    }

    template <typename T>
    void compareSet(T &target, JsonVariantConst v, T def, bool &changed) {
      T src = (v.isUnbound() || v.isNull()) ? def : v.as<T>();
      if (src == target)
        return;
      changed = true;
      target = src;
    }

    void dup(char *&target, JsonVariantConst v);
    void dup(char *&target, JsonVariantConst v, const char *def);

    void compareDup(char *&target, JsonVariantConst v, const char *def,
                    bool &changed);

    void to(JsonObject target, const char *key, const ip_addr_t &value);
    void to(JsonObject target, const char *key, const ip4_addr_t &value);
    void to(JsonObject target, const char *key, const esp_ip4_addr_t &value);
    void to(JsonObject target, const char *key, const float value);

    template <typename T>
    class Value {
     public:
      Value(T value) {
        _doc.add(value);
      }

      JsonVariantConst variant() {
        return _doc[0];
      }

     private:
      StaticJsonDocument<JSON_ARRAY_SIZE(1)> _doc;
    };

    bool check(log::Loggable *l, DynamicJsonDocument *doc, const char *msg);
    void dump(log::Loggable *l, JsonVariantConst v, const char *msg);

    class PropsContainer {
     public:
      virtual const JsonObjectConst props() const {
        return null<JsonObjectConst>();
      }
    };

  }  // namespace json

}  // namespace esp32m