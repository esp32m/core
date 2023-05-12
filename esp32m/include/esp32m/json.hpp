#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "logging.hpp"

namespace esp32m {

  namespace json {

    DynamicJsonDocument *parse(const char *data, int len,
                               DeserializationError *error = nullptr);
    DynamicJsonDocument *parse(const char *data, DeserializationError *error);
    DynamicJsonDocument *parse(const char *data);

    char *allocSerialize(const JsonVariantConst v, size_t *length = nullptr);
    std::string serialize(const JsonVariantConst v);
    size_t measure(const JsonVariantConst v);
    bool checkEqual(const JsonVariantConst a, const JsonVariantConst b);

    void checkSetResult(esp_err_t err, DynamicJsonDocument **result);

    JsonDocument &empty();
    JsonArrayConst emptyArray();
    JsonObjectConst emptyObject();

    template <class T>
    T null() {
      return empty().as<T>();
    }

    template <typename T>
    void to(JsonObject target, const char *key, const T value) {
      target[key] = value;
    }

    template <typename T>
    void to(JsonObject target, const char *key, const T value, const T def) {
      if (value != def)
        target[key] = value;
    }

    void to(JsonObject target, const char *key, const float value);
    void to(JsonObject target, const char *key, std::string value);
    void to(JsonObject target, const char *key, const char *value);
    void to(JsonObject target, const char *key, char *value);

    // isUnbound() means there is no entry with the given key/index
    // isNull means either the object with the given key is null, OR there is no
    // entry with the given key/index
    // We DO want to set the target when the source is not Unbound, even if it
    // is Null

    template <typename T>
    bool from(JsonVariantConst source, T &target, bool *changed = nullptr) {
      if (source.isUnbound() /* || source.isNull() */ ||
          source.as<T>() == target)
        return false;
      if (changed)
        *changed = true;
      target = source.as<T>();
      return true;
    }
    bool from(JsonVariantConst source, std::string &target,
              bool *changed = nullptr);
    template <typename T>
    bool fromIntCastable(JsonVariantConst source, T &target,
                         bool *changed = nullptr) {
      if (source.isUnbound() /*|| source.isNull()*/ ||
          source.as<int>() == (int)target)
        return false;
      if (changed)
        *changed = true;
      target = (T)source.as<int>();
      return true;
    }
    template <typename T>
    bool from(JsonVariantConst source, T &target, T def,
              bool *changed = nullptr) {
      T src =
          (source.isUnbound() /*|| source.isNull()*/) ? def : source.as<T>();
      if (src == target)
        return false;
      if (changed)
        *changed = true;
      target = src;
      return true;
    }
    bool from(JsonVariantConst source, std::string &target, std::string def,
              bool *changed = nullptr);

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

    class ConcatToArray {
     public:
      void add(DynamicJsonDocument *doc) {
        _documents.push_back(std::unique_ptr<DynamicJsonDocument>(doc));
      }
      DynamicJsonDocument *concat() {
        size_t mu = JSON_ARRAY_SIZE(_documents.size());
        for (auto &d : _documents) mu += d->memoryUsage();
        auto doc = new DynamicJsonDocument(mu);
        auto root = doc->to<JsonArray>();
        for (auto &d : _documents) root.add(*d);
        return doc;
      }

     private:
      std::vector<std::unique_ptr<DynamicJsonDocument> > _documents;
    };
    class ConcatToObject {
     public:
      void add(const char *key, DynamicJsonDocument *doc) {
        _documents[key] = std::unique_ptr<DynamicJsonDocument>(doc);
      }
      DynamicJsonDocument *concat() {
        size_t mu = JSON_OBJECT_SIZE(_documents.size());
        for (auto &d : _documents)
          mu += d.second->memoryUsage() + JSON_STRING_SIZE(d.first.size());
        auto doc = new DynamicJsonDocument(mu);
        auto root = doc->to<JsonObject>();
        for (auto &d : _documents) root[d.first] = *d.second;
        return doc;
      }

     private:
      std::map<std::string, std::unique_ptr<DynamicJsonDocument> > _documents;
    };

  }  // namespace json

}  // namespace esp32m