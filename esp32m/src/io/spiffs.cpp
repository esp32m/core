#include "esp32m/io/spiffs.hpp"

namespace esp32m {
  namespace spiffs {

    bool init() {
      //    esp_log_level_set("partition", ESP_LOG_DEBUG);
      if (!esp_spiffs_mounted(NULL)) {
        esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs",
                                      .partition_label = nullptr,
                                      .max_files = 5,
                                      .format_if_mount_failed = true};
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_spiffs_register(&conf)) !=
            ESP_OK)
          return false;
      }
      return true;
    }

    ConfigStore *newConfigStore() {
      if (!init())
        return nullptr;
      return new ConfigVfs("/spiffs/config.json");
    }
  }  // namespace spiffs
}  // namespace esp32m