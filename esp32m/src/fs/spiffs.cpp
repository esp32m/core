#include "esp32m/fs/spiffs.hpp"
#include "esp32m/config/vfs.hpp"

namespace esp32m {
  namespace fs {
    Spiffs::Spiffs() {
      _label = "fs";
      init();
    }

    Spiffs &Spiffs::instance() {
      static Spiffs i;
      return i;
    }

    Spiffs &useSpiffs() {
      return Spiffs::instance();
    }

    JsonDocument *Spiffs::getState(RequestContext &ctx) {
      auto doc = new JsonDocument(); /* JSON_OBJECT_SIZE(3) */
      auto root = doc->to<JsonObject>();
      size_t total, used;
      root["label"] = _label;
      if (_inited) {
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_spiffs_info(_label, &total, &used)) == ESP_OK) {
          root["total"] = total;
          root["used"] = used;
        }
      }
      return doc;
    }

    bool Spiffs::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("format")) {
        auto res = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_spiffs_format(_label));
        if (res == ESP_OK)
          req.respond();
        else
          req.respond(res);
      } else
        return false;
      return true;
    }

    bool Spiffs::init() {
      if (!_inited) {
        //    esp_log_level_set("partition", ESP_LOG_DEBUG);
        if (!esp_spiffs_mounted(_label)) {
          esp_vfs_spiffs_conf_t conf = {.base_path = "",
                                        .partition_label = _label,
                                        .max_files = 5,
                                        .format_if_mount_failed = true};
          if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_spiffs_register(&conf)) !=
              ESP_OK)
            return false;
        }
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_spiffs_check(_label)) != ESP_OK)
          ESP_ERROR_CHECK_WITHOUT_ABORT(esp_spiffs_format(_label));
        _inited = true;
        logD("root mounted");
      }
      return esp_spiffs_check(_label) == ESP_OK;
    }

  }  // namespace fs
}  // namespace esp32m