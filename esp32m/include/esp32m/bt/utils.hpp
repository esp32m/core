#pragma once

#include <esp_err.h>
#include <nimble/ble.h>

namespace esp32m {
  namespace bt {
    esp_err_t format(const ble_addr_t &addr, char *str);
    esp_err_t parse(const char *str, ble_addr_t &addr);
  }  // namespace bt
}  // namespace esp32m