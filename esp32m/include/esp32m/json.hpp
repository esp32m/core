#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

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
    void to(JsonObject target, const char *key, const T value) {
      target[key] = value;
    }

    void to(JsonObject target, const char *key, const float value);

    template <typename T>
    bool from(JsonVariantConst source, T &target, bool *changed = nullptr) {
      if (source.isUnbound() || source.isNull() || source.as<T>() == target)
        return false;
      if (changed)
        *changed = true;
      target = source.as<T>();
      return true;
    }
    template <typename T>
    bool from(JsonVariantConst source, T &target, T def,
              bool *changed = nullptr) {
      T src = (source.isUnbound() || source.isNull()) ? def : source.as<T>();
      if (src == target)
        return false;
      if (changed)
        *changed = true;
      target = src;
      return true;
    }
    bool fromDup(JsonVariantConst source, char *&target, const char *def,
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