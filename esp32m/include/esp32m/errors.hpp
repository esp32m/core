#pragma once

#include "esp32m/base.hpp"
#include "esp32m/logging.hpp"
#include "esp32m/json.hpp"

namespace esp32m {

  class ErrorItem {
   public:
    esp_err_t code;
    std::string message;
    ErrorItem(esp_err_t c) : code(c) {}
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
    esp_err_t fail(const char *message, ...) {
      va_list args;
      va_start(args, message);
      auto result = check(ESP_FAIL, message, args);
      va_end(args);
      return result;
    };
    DynamicJsonDocument *toJson(DynamicJsonDocument **result) {
      auto ls = _list.size();
      if (!ls)
        return nullptr;
      size_t size = JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(ls);
      for (auto &item : _list)
        if (item.message.size())
          size += JSON_ARRAY_SIZE(2) + JSON_STRING_SIZE(item.message.size());
        else
          size += JSON_ARRAY_SIZE(1);
      auto doc = new DynamicJsonDocument(size);
      auto root = doc->to<JsonObject>();
      auto arr = root.createNestedArray("error");
      for (auto &item : _list) {
        auto i = arr.createNestedArray();
        i.add(item.code);
        if (item.message.size())
          i.add(item.message);
      }
      if (result)
        *result = doc;
      return doc;
    }
    bool empty() {
      return _list.empty();
    }
    void concat(ErrorList &other) {
      for (auto &item : other._list) _list.push_back(item);
    }
    void dump() {
      auto doc = toJson(nullptr);
      auto str = json::allocSerialize(doc->as<JsonVariantConst>());
      delete doc;
      logi(str);
      free(str);
    }

   private:
    std::vector<ErrorItem> _list;
  };

}