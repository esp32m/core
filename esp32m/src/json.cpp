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

    void dup(char *&target, JsonVariantConst v) {
      if (target) {
        free(target);
        target = nullptr;
      }
      if (!v.isNull()) {
        const char *c = v.as<const char *>();
        if (c)
          target = strdup(c);
      }
    }

    void dup(char *&target, JsonVariantConst v, const char *def) {
      if (target && target != def)
        free(target);
      if (!v.isNull()) {
        const char *c = v.as<const char *>();
        if (!c)
          target = nullptr;
        else if (!strcmp(c, def))
          target = (char *)def;
        else
          target = strdup(c);
      } else
        target = (char *)def;
    }

#ifdef ARDUINO
    void compareSet(IPAddress &target, JsonVariantConst v, bool &changed) {
      if (v.isNull())
        return;
      const char *str = v.as<const char *>();
      IPAddress ip;
      ip.fromString(str);
      if (target == ip)
        return;
      target = ip;
      changed = true;
    }
#endif

    void compareSet(ip_addr_t &target, JsonVariantConst v, bool &changed) {
      if (v.isUnbound() || v.isNull())
        return;
      const char *str = v.as<const char *>();
      if (!str)
        return;
      ip_addr_t addr;
      if (!ipaddr_aton(str, &addr) || ip_addr_cmp(&target, &addr))
        return;
      ip_addr_copy(target, addr);
      changed = true;
    }

    void compareSet(ip4_addr_t &target, JsonVariantConst v, bool &changed) {
      if (v.isUnbound() || v.isNull())
        return;
      const char *str = v.as<const char *>();
      if (!str)
        return;
      ip4_addr_t addr;
      if (!ip4addr_aton(str, &addr) || ip4_addr_cmp(&target, &addr))
        return;
      ip4_addr_copy(target, addr);
      changed = true;
    }

    void compareSet(esp_ip4_addr_t &target, JsonVariantConst v, bool &changed) {
      if (v.isUnbound() || v.isNull())
        return;
      const char *str = v.as<const char *>();
      if (!str)
        return;
      uint32_t addr = esp_ip4addr_aton(str);
      if (target.addr == addr)
        return;
      target.addr = addr;
      changed = true;
    }

    void to(JsonObject target, const char *key, const esp_ip4_addr_t &value) {
      if (!value.addr)
        return;
      char buf[16];
      esp_ip4addr_ntoa(&value, buf, sizeof(buf));
      target[key] = buf;
    }

    void to(JsonObject target, const char *key, const ip_addr_t &value) {
      if (ip_addr_isany(&value))
        return;
      char buf[40];
      ipaddr_ntoa_r(&value, buf, sizeof(buf));
      target[key] = buf;
    }

    void to(JsonVariant target, const char *key, const ip4_addr_t &value) {
      if (ip4_addr_isany_val(value))
        return;
      char buf[16];
      ip4addr_ntoa_r(&value, buf, sizeof(buf));
      target[key] = buf;
    }

    void to(JsonObject target, const char *key, const float value) {
      if (isnan(value))
        return;
      target[key] = value;
    }

    void compareDup(char *&target, JsonVariantConst v, const char *def,
                    bool &changed) {
      if (target && target != def) {
        free(target);
        changed = true;
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
          changed = true;
        }
      } else
        changed = true;
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