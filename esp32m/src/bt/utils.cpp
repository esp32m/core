#include "sdkconfig.h"

#ifdef CONFIG_BT_ENABLED

#  include "esp32m/bt/utils.hpp"

namespace esp32m {
  namespace bt {
    const char *addrFormat = "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX";

    esp_err_t format(const ble_addr_t &addr, char *str) {
      if (!str)
        return ESP_ERR_INVALID_ARG;
      if (sprintf(str, addrFormat, addr.val[5], addr.val[4], addr.val[3],
                  addr.val[2], addr.val[1], addr.val[0]) != 17)
        return ESP_ERR_INVALID_ARG;
      return ESP_OK;
    }

    esp_err_t parse(const char *str, ble_addr_t &addr) {
      auto l = str ? strlen(str) : 0;
      if (!l)
        return ESP_ERR_INVALID_ARG;
      addr.type = 0;
      if (sscanf(str, addrFormat, &addr.val[5], &addr.val[4], &addr.val[3],
                 &addr.val[2], &addr.val[1], &addr.val[0]) != 6)
        return ESP_ERR_INVALID_ARG;
      return ESP_OK;
    }
  }  // namespace bt
}  // namespace esp32m

#endif