#include "esp32m/json.hpp"

#include "esp32m/net/wifi_utils.hpp"

namespace esp32m {

  const char *bssidFormat = "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX";

  bool formatBssid(const uint8_t *bssid, char *bssidStr) {
    if (!bssid)
      return false;
    return sprintf(bssidStr, bssidFormat, bssid[0], bssid[1], bssid[2],
                   bssid[3], bssid[4], bssid[5]) == 17;
  }
  bool parseBssid(const char *bssidStr, uint8_t *bssid) {
    if (!bssidStr)
      return false;
    return sscanf(bssidStr, bssidFormat, &bssid[0], &bssid[1], &bssid[2],
                  &bssid[3], &bssid[4], &bssid[5]) == 6;
  }

  void ip2json(JsonObject json, const esp_netif_ip_info_t &source) {
    json::to(json, "ip", source.ip);
    json::to(json, "gw", source.gw);
    json::to(json, "mask", source.netmask);
  }

  void json2ip(JsonObjectConst json, esp_netif_ip_info_t &target,
               bool &changed) {
    json::compareSet(target.ip, json["ip"], changed);
    json::compareSet(target.gw, json["gw"], changed);
    json::compareSet(target.netmask, json["mask"], changed);
  }

}  // namespace esp32m