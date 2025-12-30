#include "esp32m/json.hpp"
#include "esp32m/logging.hpp"

#include <errno.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>

namespace esp32m {
  namespace json {

    JsonDocument &empty() {
      static JsonDocument doc;
      return doc;
    }

    JsonArrayConst emptyArray() {
      static JsonDocument doc;
      if (doc.isNull())
        doc.to<JsonArray>();
      return doc.as<JsonArrayConst>();
    }

    JsonObjectConst emptyObject() {
      static JsonDocument doc;
      if (doc.isNull())
        doc.to<JsonObject>();
      return doc.as<JsonObjectConst>();
    }

    JsonDocument *parse(const char *data, int len,
                        DeserializationError *error) {
      if (data && len < 0)
        len = strlen(data);
      if (!data || !len) {
        if (error)
          *error = DeserializationError::EmptyInput;
        return nullptr;
      }
      /*size_t ds = len * 3;
      for (;;) {*/
      /*JsonDocument *doc = nullptr;
      if (ds + 4096 < heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL))
        doc = new (std::nothrow) JsonDocument(ds);
      if (!doc)
        return nullptr;*/
      auto doc = new JsonDocument();
      auto r = deserializeJson(*doc, data, len);
      if (r == DeserializationError::Ok) {
        doc->shrinkToFit();
        return doc;
      }
      delete doc;
      if (error)
        *error = r;
      else /*if (r != DeserializationError::NoMemory)*/ {
        // we can't safely pass data as a parameter here, because it may not
        // be null-terminated
        char *str = strndup(data, len);
        logw("JSON error %s when parsing %s", r.c_str(), str);
        free(str);
      }
      /*ds *= 4;
      ds /= 3;
    }*/
      return nullptr;
    }
    JsonDocument *parse(const char *data, DeserializationError *error) {
      return parse(data, -1, error);
    }

    JsonDocument *parse(const char *data) {
      return parse(data, -1, nullptr);
    }

    /*char *allocSerialize(const JsonVariantConst v, size_t *length) {
      // measureJson() excludes null terminator
      size_t dl = measureJson(v) + 1;
      char *ds = (char *)malloc(dl);
      size_t actual = serializeJson(v, ds, dl);
      dl--;
      if (actual != dl)
        logw("size of serialized JSON is different from expected: %d!=%d",
             actual, dl);
      auto sl = strlen(ds);
      if (actual != sl)
        logw("size of serialized JSON is different from string length: %d!=%d",
             actual, sl);
      if (length)
        *length = actual;
      return ds;
    }

    std::string serialize(const JsonVariantConst v) {
      std::string result;
      auto buf = allocSerialize(v);
      if (buf) {
        result = buf;
        free(buf);
      }
      return result;
    }*/

    bool checkEqual(const JsonVariantConst a, const JsonVariantConst b) {
      std::string sa, sb;
      serializeJson(a, sa);
      serializeJson(b, sb);
      bool result = sa == sb;
      if (!result)
        logw("expected JSON variants to be equal: %s != %s", sa, sb);

      /*auto sa = allocSerialize(a);
      auto sb = allocSerialize(b);
      bool result = strcmp(sa, sb) == 0;
      if (!result)
        logw("expected JSON variants to be equal: %s != %s", sa, sb);
      free(sa);
      free(sb);
      */
      return result;
    }

    void to(JsonObject target, const char *key, const float value) {
      if (isnan(value))
        return;
      target[key] = value;
    }
    void to(JsonObject target, const char *key, std::string value) {
      if (value.empty())
        return;
      target[key] = value;
    }
    void to(JsonObject target, const char *key, const char *value) {
      if (!value || !strlen(value))
        return;
      target[key] = value;
    }
    void to(JsonObject target, const char *key, char *value) {
      if (!value || !strlen(value))
        return;
      target[key] = value;
    }

    bool from(JsonVariantConst source, std::string &target, bool *changed) {
      if (source.isUnbound())
        return false;
      // special handling for std::string because null.as<std::string>() returns
      // "null" rather than empty string (documented behavior)
      std::string src;
      if (!source.isNull())
        src = source.as<std::string>();
      if (src == target)
        return false;
      if (changed)
        *changed = true;
      target = src;
      return true;
    }

    bool from(JsonVariantConst source, std::string &target, std::string def,
              bool *changed) {
      std::string src;
      if (source.isUnbound())
        src = def;
      else if (!source.isNull())
        src = source.as<std::string>();
      if (src == target)
        return false;
      if (changed)
        *changed = true;
      target = src;
      return true;
    }

    bool check(log::Loggable *l, JsonDocument *doc, const char *msg) {
      if (!doc)
        return false;
      if (!doc->overflowed())
        return true;
      auto &logger = l ? l->logger() : log::system();
      logger.logf(log::Level::Warning, "json document overflow: %s", msg);
      return false;
    }

    void dump(log::Loggable *l, JsonVariantConst v, const char *msg) {
      // char *data = allocSerialize(v);
      std::string data;
      serializeJson(v, data);
      l->logger().logf(log::Level::Debug, "%s: %s", msg, data.c_str());
      /*if (data)
        free(data);*/
    }

    void saveToFile(const char *path, JsonVariantConst v) {
      if (!path || !*path) {
        logw("saveToFile: empty path");
        return;
      }
      FILE *f = fopen(path, "wb");
      if (!f) {
        logw("saveToFile: failed to open %s: %s", path, strerror(errno));
        return;
      }

      std::string data;
      serializeJson(v, data);

      if (!data.empty()) {
        auto written = fwrite(data.data(), 1, data.size(), f);
        if (written != data.size())
          logw("saveToFile: short write to %s: %d/%d", path, (int)written,
               (int)data.size());
      }

      if (fclose(f) != 0)
        logw("saveToFile: failed to close %s: %s", path, strerror(errno));
    }

    std::unique_ptr<JsonDocument> loadFromFile(const char *path) {
      if (!path || !*path) {
        logw("loadFromFile: empty path");
        return nullptr;
      }
      FILE *f = fopen(path, "rb");
      if (!f) {
        logw("loadFromFile: failed to open %s: %s", path, strerror(errno));
        return nullptr;
      }

      if (fseek(f, 0, SEEK_END) != 0) {
        logw("loadFromFile: fseek failed for %s: %s", path, strerror(errno));
        fclose(f);
        return nullptr;
      }
      long size = ftell(f);
      if (size < 0) {
        logw("loadFromFile: ftell failed for %s: %s", path, strerror(errno));
        fclose(f);
        return nullptr;
      }
      if (fseek(f, 0, SEEK_SET) != 0) {
        logw("loadFromFile: fseek(set) failed for %s: %s", path,
             strerror(errno));
        fclose(f);
        return nullptr;
      }
      if (size == 0) {
        fclose(f);
        return nullptr;
      }

      // +1 just to allow optional NUL-termination for debugging/logging.
      size_t len = (size_t)size;
      char *buf = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_8BIT);
      if (!buf) {
        logw("loadFromFile: OOM reading %s (%d bytes)", path, (int)len);
        fclose(f);
        return nullptr;
      }

      size_t read = fread(buf, 1, len, f);
      fclose(f);
      if (read != len) {
        logw("loadFromFile: short read from %s: %d/%d", path, (int)read,
             (int)len);
        free(buf);
        return nullptr;
      }
      buf[len] = 0;

      DeserializationError err;
      JsonDocument *doc = parse(buf, (int)len, &err);
      free(buf);
      if (!doc) {
        logw("loadFromFile: JSON parse error %s in %s", err.c_str(), path);
        return nullptr;
      }
      return std::unique_ptr<JsonDocument>(doc);
    }

  }  // namespace json
}  // namespace esp32m