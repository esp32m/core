#pragma once

#include <esp_spiffs.h>

#include "esp32m/config/vfs.hpp"

namespace esp32m {

  namespace spiffs {
    bool init();
    ConfigStore *newConfigStore();
  }  // namespace spiffs

}  // namespace esp32m
