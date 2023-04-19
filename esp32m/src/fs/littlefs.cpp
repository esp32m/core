#include <esp_littlefs.h>

#include "esp32m/fs/littlefs.hpp"
#include "esp32m/config/vfs.hpp"

namespace esp32m {
  namespace fs {

    Littlefs::Littlefs() {
      _label = "littlefs";
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
        esp_vfs_littlefs_conf_t conf = {
            .base_path = "/root",
            .partition_label = _label,
            .format_if_mount_failed = true,
            .dont_mount = false,
        };
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_littlefs_register(&conf)) !=
            ESP_OK)
          return false;
        _inited = true;
      }
      return _inited;
    }

    ConfigStore *Littlefs::newConfigStore() {
      if (!init())
        return nullptr;
      return new ConfigVfs("/root/config.json");
    }
  }  // namespace io
}  // namespace esp32m