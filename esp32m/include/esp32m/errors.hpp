#pragma once

#include "esp32m/base.hpp"
#include "esp32m/json.hpp"
#include "esp32m/logging.hpp"

#define ESP_ERR_ESP32M_BASE 0x6000
#define ESP_ERR_ESP32M_END (ESP_ERR_ESP32M_BASE + 0x1000)

namespace esp32m {

  constexpr const char *ErrBusy = "ERR_BUSY";

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
    /*size_t jsonSize() {
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
    }*/
    void toJson(JsonVariant target) const {
      JsonArray arr;
      if (target.is<JsonObject>())
        arr = target["error"].to<JsonArray>();
      else
        arr = target.as<JsonArray>();
      for (auto &item : _list) {
        auto i = arr.add<JsonArray>();
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
    /*JsonDocument *toJson(JsonDocument **result) const {
      auto doc = new JsonDocument();
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
    }*/
    void toJson(std::unique_ptr<JsonDocument> &result) const {
      if (empty()) return;
      JsonVariant target;
      if (result)  // TODO: make this more transparent (quick hack to include
                   // "error" object in case we are responding from
                   // setConfig/setState)
        target = result->as<JsonObject>();
      else {
        auto doc = new JsonDocument();
        target = doc->to<JsonObject>();
        result.reset(doc);
      }
      toJson(target);
    }

    bool empty() const {
      return _list.empty();
    }
    void copyFrom(ErrorList &other) {
      for (auto &item : other._list) _list.push_back(item);
    }
    void dump() const {
      if (empty())
        return;
      std::unique_ptr<JsonDocument> doc;
      toJson(doc);
      std::string str;
      serializeJson(doc->as<JsonVariantConst>(), str);
      // auto str = json::allocSerialize(doc->as<JsonVariantConst>());
      loge(str.c_str());
      // free(str);
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