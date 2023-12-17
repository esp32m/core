#pragma once

#include "esp32m/base.hpp"
#include "esp32m/json.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {

  constexpr const char * ErrBusy = "ERR_BUSY";

  class ErrorItem {
   public:
    esp_err_t code;
    std::string name;
    std::string message;
    ErrorItem(esp_err_t c) : code(c) {}
    ErrorItem(std::string name) : code(ESP_OK), name(name) {}
    ErrorItem(esp_err_t c, std::string m) : code(c), message(m) {}
  };
  class ErrorList {
   public:
    esp_err_t check(esp_err_t err) {
      if (err != ESP_OK)
        _list.emplace_back(err);
      return err;
    };
    esp_err_t check(esp_err_t err, const char *message, ...) {
      if (err != ESP_OK) {
        va_list args;
        va_start(args, message);
        check(err, message, args);
        va_end(args);
      }
      return err;
    };
    esp_err_t check(esp_err_t err, const char *message, va_list args) {
      if (err != ESP_OK) {
        std::string msg = string_printf(message, args);
        _list.emplace_back(err, msg);
      }
      return err;
    };
    void add(const char *name, ...) {
      va_list args;
      va_start(args, name);
      add(name, args);
      va_end(args);
    };
    void add(const char *name, va_list args) {
      std::string n = string_printf(name, args);
      _list.emplace_back(n);
    };
    esp_err_t fail(const char *message, ...) {
      va_list args;
      va_start(args, message);
      auto result = check(ESP_FAIL, message, args);
      va_end(args);
      return result;
    };
    size_t jsonSize() {
      auto ls = _list.size();
      if (!ls)
        return 0;
      size_t size = JSON_ARRAY_SIZE(ls);
      for (auto &item : _list) {
        size += JSON_ARRAY_SIZE(1);
        if (item.name.size())
          size += JSON_STRING_SIZE(item.name.size());
        if (item.message.size())
          size += JSON_ARRAY_SIZE(1) + JSON_STRING_SIZE(item.message.size());
      }
      return size;
    }
    void toJson(JsonVariant target) {
      JsonArray arr;
      if (target.is<JsonObject>())
        arr = target.createNestedArray("error");
      else
        arr = target.as<JsonArray>();
      for (auto &item : _list) {
        auto i = arr.createNestedArray();
        if (item.name.size())
          i.add(item.name);
        else {
          auto name = esp_err_to_name(item.code);
          if (name && strcmp(name, "ERROR") && strcmp(name, "UNKNOWN ERROR"))
            i.add(name);
          else
            i.add(item.code);
        }
        if (item.message.size())
          i.add(item.message);
      }
    }
    DynamicJsonDocument *toJson(DynamicJsonDocument **result) {
      size_t size = jsonSize();
      if (!size)
        return nullptr;
      auto doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1) + size);
      JsonVariant target;
      if (result)  // TODO: make this more transparent (quick hack to include
                   // "error" object in case we are responding from
                   // setConfig/setState)
        target = doc->to<JsonObject>();
      else
        target = doc->to<JsonArray>();
      toJson(target);
      if (result)
        *result = doc;
      return doc;
    }
    bool empty() {
      return _list.empty();
    }
    void copyFrom(ErrorList &other) {
      for (auto &item : other._list) _list.push_back(item);
    }
    void dump() {
      if (empty())
        return;
      auto doc = toJson(nullptr);
      auto str = json::allocSerialize(doc->as<JsonVariantConst>());
      delete doc;
      loge(str);
      free(str);
    }
    void clear() {
      _list.clear();
    }
    void append(ErrorList &other) {
      for (auto &item : other._list) _list.push_back(item);
    }

   private:
    std::vector<ErrorItem> _list;
  };

}  // namespace esp32m