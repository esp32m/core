#pragma once

#include <ArduinoJson.h>
#include <dhcpserver/dhcpserver.h>
#include <dhcpserver/dhcpserver_options.h>
#include <esp_err.h>
#include <esp_netif.h>
#include <lwip/ip_addr.h>
#include <map>
#include <vector>

namespace esp32m {

  namespace json {

    template <typename T>
    bool copyVector(std::vector<T> &source, std::vector<T> &target,
                    bool *changed = nullptr) {
      bool c = source.size() != target.size();
      if (!c)
        for (int i = 0; i < source.size(); i++)
          if (memcmp(&source[i], &target[i], sizeof(T))) {
            c = true;
            break;
          }
      if (!c)
        return false;
      target.clear();
      for (int i = 0; i < source.size(); i++) target.push_back(source[i]);
      if (changed)
        *changed = true;
      return true;
    }

    void to(JsonObject target, const char *key, const ip_addr_t &value);
    void to(JsonObject target, const char *key, const ip4_addr_t &value);
    void to(JsonObject target, const char *key, const esp_ip4_addr_t &value);
    void to(JsonObject target, const esp_netif_ip_info_t &source);
    void to(JsonObject target, esp_ip6_addr_t *ip6, int count);
    void to(JsonObject target,
            std::map<esp_netif_dns_type_t, esp_netif_dns_info_t> &source);
    void to(JsonObject target, const dhcps_lease_t &source);
    void macTo(JsonObject target, const uint8_t mac[]);

    bool from(JsonVariantConst source, ip_addr_t &target,
              bool *changed = nullptr);
    bool from(JsonVariantConst source, ip4_addr_t &target,
              bool *changed = nullptr);
    bool from(JsonVariantConst source, esp_ip4_addr_t &target,
              bool *changed = nullptr);
    bool from(JsonVariantConst source, esp_netif_ip_info_t &target,
              bool *changed = nullptr);
    bool from(JsonVariantConst source, std::vector<esp_ip6_addr_t> &target,
              bool *changed = nullptr);
    bool from(JsonVariantConst source,
              std::map<esp_netif_dns_type_t, esp_netif_dns_info_t> &target,
              bool *changed = nullptr);
    bool from(JsonVariantConst source, dhcps_lease_t &target,
              bool *changed = nullptr);
    bool macFrom(JsonVariantConst source, uint8_t target[],
                 bool *changed = nullptr);
  }  // namespace json

  namespace net {

    const int Ipv4MaxChars = 16;
    const int IpInfoJsonSize =
        JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(3) + (Ipv4MaxChars * 3);
    const int DhcpsLeaseJsonSize =
        JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(3) + (Ipv4MaxChars * 2);
    const int Ipv6MaxChars = 40;
    const int MacMaxChars = 18;

    bool isEmptyMac(uint8_t *mac);
    bool isEmpty(esp_netif_dns_info_t* dns);

    esp_err_t useNetif();
    esp_err_t useEventLoop();
  }  // namespace net
}  // namespace esp32m