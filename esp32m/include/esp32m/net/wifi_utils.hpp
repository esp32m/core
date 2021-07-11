#pragma once

#include <esp_netif.h>

#include <ArduinoJson.h>

namespace esp32m {

  bool formatBssid(const uint8_t *bssid, char *bssidStr);
  bool parseBssid(const char *bssidStr, uint8_t *bssid);

  void ip2json(JsonObject json, const esp_netif_ip_info_t &source);
  void json2ip(JsonObjectConst json, esp_netif_ip_info_t &target,
               bool &changed);

}  // namespace esp32m