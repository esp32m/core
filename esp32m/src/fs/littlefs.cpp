#include <esp_littlefs.h>

#include "esp32m/config/vfs.hpp"
#include "esp32m/fs/littlefs.hpp"

namespace esp32m {
  namespace fs {

    Littlefs::Littlefs() {
      _label = "spiffs";
      init();
    }

    Littlefs &Littlefs::instance() {
      static Littlefs i;
      return i;
    }

    Littlefs &useLittlefs() {
      return Littlefs::instance();
    }

    DynamicJsonDocument *Littlefs::getState(const JsonVariantConst args) {
      auto doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(3));
      auto root = doc->to<JsonObject>();
      size_t total, used;
      root["label"] = _label;
      if (_inited) {
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_littlefs_info(_label, &total, &used)) == ESP_OK) {
          root["total"] = total;
          root["used"] = used;
        }
      }
      return doc;
    }

    bool Littlefs::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("format")) {
        auto res = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_littlefs_format(_label));
        if (res == ESP_OK)
          req.respond();
        else
          req.respond(res);
      } else
        return false;
      return true;
    }

    bool Littlefs::init() {
      if (!_inited) {
        esp_vfs_littlefs_conf_t conf = {};
        conf.base_path = "";
        conf.partition_label = _label;
        conf.format_if_mount_failed = true;
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_littlefs_register(&conf)) !=
            ESP_OK)
          return false;
        _inited = true;
        logD("root mounted");
      }
      return _inited;
    }

  }  // namespace fs
}  // namespace esp32m