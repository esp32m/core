#include "esp32m/json.hpp"
#include "esp32m/logging.hpp"

#include <esp_heap_caps.h>
#include <math.h>

namespace esp32m {
  namespace json {

    StaticJsonDocument<0> EmptyDocument;

    JsonDocument &empty() {
      return EmptyDocument;
    }

    DynamicJsonDocument *parse(const char *data, int len) {
      if (data && len < 0)
        len = strlen(data);
      if (!data || !len)
        return nullptr;
      size_t ds = len * 3;
      for (;;) {
        DynamicJsonDocument *doc = nullptr;
        if (ds + 4096 < heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL))
          doc = new (std::nothrow) DynamicJsonDocument(ds);
        if (!doc)
          return nullptr;
        auto r = deserializeJson(*doc, data, len);
        if (r == DeserializationError::Ok) {
          doc->shrinkToFit();
          return doc;
        }
        delete doc;
        if (r != DeserializationError::NoMemory) {
          // we can't safely pass data as a parameter here, because it may not
          // be null-terminated
          char *str = strndup(data, len);
          logw("JSON error %s when parsing %s", r.c_str(), str);
          free(str);
          return nullptr;
        }
        ds *= 4;
        ds /= 3;
      }
    }

    char *allocSerialize(const JsonVariantConst v) {
      size_t dl = measureJson(v) + 1;
      char *ds = (char *)malloc(dl);
      serializeJson(v, ds, dl);
      return ds;
    }

    size_t measure(const JsonVariantConst v) {
      size_t result = 0;
      if (v.is<JsonArray>() || v.is<JsonArrayConst>()) {
        JsonArrayConst a = v.as<JsonArrayConst>();
        result += JSON_ARRAY_SIZE(a.size());
        for (JsonVariantConst i : a) result += measure(i);
      } else if (v.is<JsonObject>() || v.is<JsonObjectConst>()) {
        JsonObjectConst o = v.as<JsonObjectConst>();
        result += JSON_OBJECT_SIZE(o.size());
        for (JsonPairConst kv : o)
          result += JSON_STRING_SIZE(strlen(kv.key().c_str())) + 1 +
                    measure(kv.value());
      } else if (v.is<const char *>()) {
        const char *c = v.as<const char *>();
        result += JSON_STRING_SIZE(strlen(c)) + 1;
      }
      return result;
    }

    void checkSetResult(esp_err_t err, DynamicJsonDocument **result) {
      if (err == ESP_OK)
        return;
      DynamicJsonDocument *doc = *result;
      if (!doc)
        doc = *result = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
      JsonObject root = doc->to<JsonObject>();
      root["error"] = err;
    }

    void to(JsonObject target, const char *key, const float value) {
      if (isnan(value))
        return;
      target[key] = value;
    }

    bool from(JsonVariantConst source, std::string &target,
              bool *changed = nullptr) {
      if (source.isUnbound() || source.isNull() ||
          source.as<std::string>() == target)
        return false;
      if (changed)
        *changed = true;
      target = source.as<std::string>();
      return true;
    }

    bool from(JsonVariantConst source, std::string &target, std::string def,
              bool *changed = nullptr) {
      std::string src = (source.isUnbound() || source.isNull())
                            ? def
                            : source.as<std::string>();
      if (src == target)
        return false;
      if (changed)
        *changed = true;
      target = src;
      return true;
    }
    
    bool fromDup(JsonVariantConst v, char *&target, const char *def,
                 bool *changed) {
      bool c = false;
      if (target && target != def) {
        free(target);
        c = true;
      }
      bool setToDefault = false;
      if (!v.isNull()) {
        const char *c = v.as<const char *>();
        if (!c)
          target = nullptr;
        else if (def && !strcmp(c, def))
          setToDefault = true;
        else
          target = strdup(c);
      } else
        setToDefault = true;
      if (setToDefault) {
        if (target != def) {
          target = (char *)def;
          c = true;
        }
      } else
        c = true;
      if (c && changed)
        *changed = true;
      return c;
    }

    bool check(log::Loggable *l, DynamicJsonDocument *doc, const char *msg) {
      if (!doc || !l)
        return false;
      if (!doc->overflowed())
        return true;
      l->logger().logf(log::Level::Warning,
                       "json document overflow: %s, capacity=%d, usage=%d", msg,
                       doc->capacity(), doc->memoryUsage());
      return false;
    }

    void dump(log::Loggable *l, JsonVariantConst v, const char *msg) {
      char *data = allocSerialize(v);
      l->logger().logf(log::Level::Debug, "%s: %s", msg, data);
      if (data)
        free(data);
    }

  }  // namespace json
}  // namespace esp32m