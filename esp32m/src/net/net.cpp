#include "esp32m/net/net.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/json.hpp"

#include <esp_event.h>
#include <esp_mac.h>
#include <lwip/dns.h>
#include <lwip/sockets.h>

namespace esp32m {
  namespace json {

    void to(JsonObject target, const char *key, const esp_ip4_addr_t &value) {
      if (!value.addr)
        return;
      char buf[net::Ipv4MaxChars];
      esp_ip4addr_ntoa(&value, buf, sizeof(buf));
      target[key] = buf;
    }

    void to(JsonObject target, const char *key, const ip_addr_t &value) {
      if (ip_addr_isany(&value))
        return;
      char buf[net::Ipv6MaxChars];
      ipaddr_ntoa_r(&value, buf, sizeof(buf));
      target[key] = buf;
    }

    void to(JsonVariant target, const char *key, const ip4_addr_t &value) {
      if (ip4_addr_isany_val(value))
        return;
      char buf[net::Ipv4MaxChars];
      ip4addr_ntoa_r(&value, buf, sizeof(buf));
      target[key] = buf;
    }

    void to(JsonObject target, const esp_netif_ip_info_t &source) {
      char buf[net::Ipv4MaxChars];
      auto arr = target.createNestedArray("ip");
      esp_ip4addr_ntoa(&source.ip, buf, sizeof(buf));
      arr.add(buf);
      esp_ip4addr_ntoa(&source.gw, buf, sizeof(buf));
      arr.add(buf);
      esp_ip4addr_ntoa(&source.netmask, buf, sizeof(buf));
      arr.add(buf);
    }

    void to(JsonObject target, esp_ip6_addr_t *ip6, int count) {
      char buf[net::Ipv6MaxChars];
      if (count == 0)
        return;
      auto arr = target.createNestedArray("ipv6");
      for (int i = 0; i < count; i++) {
        ip6addr_ntoa_r((ip6_addr_t *)&ip6[i], buf, sizeof(buf));
        arr.add(buf);
      }
    }

    void to(JsonObject target,
            std::map<esp_netif_dns_type_t, esp_netif_dns_info_t> &source) {
      char buf[net::Ipv6MaxChars];
      if (source.size() == 0)
        return;
      auto arr = target.createNestedArray("dns");
      for (int i = 0; i < ESP_NETIF_DNS_MAX; i++) {
        auto it = source.find((esp_netif_dns_type_t)i);
        bool valid = false;
        if (it != source.end()) {
          esp_ip_addr_t *ip = &it->second.ip;
          int af = -1;
          switch (ip->type) {
            case IPADDR_TYPE_V4:
              af = AF_INET;
              break;
            case IPADDR_TYPE_V6:
              af = AF_INET6;
              break;
            default:
              break;
          }
          valid = af != -1 &&
                  inet_ntop(af, &ip->u_addr, buf, net::Ipv6MaxChars) != nullptr;
        }
        if (valid)
          arr.add(buf);
        else
          arr.add((const char *)nullptr);
      }
    }
    void to(JsonObject target, const dhcps_lease_t &source) {
      char buf[net::Ipv4MaxChars];
      auto arr = target.createNestedArray("lease");
      arr.add(source.enable);
      ip4addr_ntoa_r(&source.start_ip, buf, sizeof(buf));
      arr.add(buf);
      ip4addr_ntoa_r(&source.end_ip, buf, sizeof(buf));
      arr.add(buf);
    }

    void macTo(JsonObject target, const uint8_t mac[]) {
      macTo(target, "mac", mac);
    }
    void macTo(JsonObject target, const char *key, const uint8_t mac[]) {
      char buf[net::MacMaxChars];
      sprintf(buf, MACSTR, MAC2STR(mac));
      target[key] = buf;
    }
    bool macTo(JsonArray target, const uint8_t mac[]) {
      char buf[net::MacMaxChars];
      sprintf(buf, MACSTR, MAC2STR(mac));
      return target.add(buf);
    }

    void interfacesTo(JsonObject target) {
      auto nr = esp_netif_get_nr_of_ifs();
      if (nr == 0)
        return;
      auto ifaces = target.createNestedArray("interfaces");
      esp_netif_t *cif = nullptr;
      for (;;) {
        cif = esp_netif_next(cif);
        if (!cif)
          break;
        auto a = ifaces.createNestedArray();
        a.add(esp_netif_get_ifkey(cif));
        a.add(esp_netif_get_desc(cif));
        a.add(esp_netif_get_netif_impl_index(cif));
        char in[NETIF_NAMESIZE] = {'\0'};
        if (esp_netif_get_netif_impl_name(cif, in) == ESP_OK && strlen(in))
          a.add(in);
      }
    }
    size_t interfacesSize() {
      auto nr = esp_netif_get_nr_of_ifs();
      if (nr == 0)
        return 0;
      return JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(nr) +
             nr * JSON_ARRAY_SIZE(4) + nr * JSON_STRING_SIZE(NETIF_NAMESIZE);
    }

    bool from(JsonVariantConst source, ip_addr_t &target, bool *changed) {
      if (source.isUnbound() || source.isNull())
        return false;
      const char *str = source.as<const char *>();
      if (!str)
        return false;
      ip_addr_t addr;
      if (!ipaddr_aton(str, &addr) || ip_addr_cmp(&target, &addr))
        return false;
      ip_addr_copy(target, addr);
      if (changed)
        *changed = true;
      return true;
    }

    bool from(JsonVariantConst source, ip4_addr_t &target, bool *changed) {
      if (source.isUnbound() || source.isNull())
        return false;
      const char *str = source.as<const char *>();
      if (!str)
        return false;
      ip4_addr_t addr;
      if (!ip4addr_aton(str, &addr) || ip4_addr_cmp(&target, &addr))
        return false;
      ip4_addr_copy(target, addr);
      if (changed)
        *changed = true;
      return true;
    }

    bool from(JsonVariantConst source, esp_ip4_addr_t &target, bool *changed) {
      if (source.isUnbound() || source.isNull())
        return false;
      const char *str = source.as<const char *>();
      if (!str)
        return false;
      uint32_t addr = esp_ip4addr_aton(str);
      if (target.addr == addr)
        return false;
      target.addr = addr;
      if (changed)
        *changed = true;
      return true;
    }

    bool from(JsonVariantConst source, esp_netif_ip_info_t &target,
              bool *changed) {
      auto arr = source["ip"].as<JsonArrayConst>();
      if (!arr)
        return false;
      auto c = false;
      auto size = arr.size();
      if (size > 0)
        c |= from(arr[0], target.ip, changed);
      if (size > 1)
        c |= from(arr[1], target.gw, changed);
      if (size > 2)
        c |= from(arr[2], target.netmask, changed);
      return c;
    }

    bool from(JsonVariantConst source, std::vector<esp_ip6_addr_t> &target,
              bool *changed) {
      auto arr = source["ipv6"].as<JsonArrayConst>();
      if (!arr)
        return false;
      std::vector<esp_ip6_addr_t> a;
      esp_ip6_addr_t addr;
      for (JsonVariantConst v : arr) {
        auto s = v.as<const char *>();
        if (s && esp_netif_str_to_ip6(s, &addr) == ESP_OK)
          a.push_back(addr);
      }
      return copyVector(a, target, changed);
    }
    bool from(JsonVariantConst source,
              std::map<esp_netif_dns_type_t, esp_netif_dns_info_t> &target,
              bool *changed) {
      auto arr = source["dns"].as<JsonArrayConst>();
      if (!arr)
        return false;
      bool c = false;
      esp_netif_dns_info_t dns;
      for (auto i = 0; i < ESP_NETIF_DNS_MAX; i++) {
        bool valid = false;
        auto s = arr[i].as<const char *>();
        if (s) {
          if (inet_pton(AF_INET, s, &dns.ip.u_addr)) {
            dns.ip.type = IPADDR_TYPE_V4;
            valid = true;
          } else if (inet_pton(AF_INET6, s, &dns.ip.u_addr)) {
            dns.ip.type = IPADDR_TYPE_V6;
            valid = true;
          }
        }
        auto found = target.find((esp_netif_dns_type_t)i);
        auto exists = found != target.end();
        auto same =
            valid == exists && (!valid || memcmp(&(found->second), &dns,
                                                 sizeof(esp_netif_dns_info_t)));
        if (!same) {
          if (exists)
            target.erase(found);
          else
            target[(esp_netif_dns_type_t)i] = dns;
          if (*changed)
            *changed = true;
          c = true;
        }
      }
      return c;
    }
    bool from(JsonVariantConst source, dhcps_lease_t &target, bool *changed) {
      auto arr = source["lease"].as<JsonArrayConst>();
      if (!arr)
        return false;
      auto c = false;
      auto size = arr.size();
      if (size > 0)
        c |= from(arr[0], target.enable, changed);
      if (size > 1)
        c |= from(arr[1], target.start_ip, changed);
      if (size > 2)
        c |= from(arr[2], target.end_ip, changed);
      return c;
    }

    bool macFrom(JsonVariantConst source, uint8_t target[], bool *changed) {
      auto str = source.as<const char *>();
      uint8_t mac[6];
      if (!net::macParse(str, mac))
        return false;
      bool c = false;
      for (auto i = 0; i < 6; i++)
        if (mac[i] != target[i]) {
          target[i] = mac[i];
          c = true;
        }
      if (c && changed)
        *changed = true;
      return c;
    }
  }  // namespace json

  namespace net {

    bool macParse(const char *macstr, uint8_t target[]) {
      if (!macstr)
        return false;
      return sscanf(macstr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &target[0],
                    &target[1], &target[2], &target[3], &target[4],
                    &target[5]) == 6;
    }
    bool isEmptyMac(uint8_t *mac) {
      if (!mac)
        return true;
      for (auto i = 0; i < 6; i++)
        if (mac[i])
          return false;
      return true;
    }
    bool isEmpty(esp_netif_dns_info_t *dns) {
      if (!dns)
        return true;
      switch (dns->ip.type) {
        case IPADDR_TYPE_V4:
          return dns->ip.u_addr.ip4.addr == 0;
        case IPADDR_TYPE_V6:
          return esp_netif_ip6_get_addr_type(&dns->ip.u_addr.ip6) ==
                 ESP_IP6_ADDR_IS_UNKNOWN;
        default:
          return true;
      }
    }

    bool isAnyNetifUp() {
      esp_netif_t *cif = nullptr;
      for (;;) {
        cif = esp_netif_next(cif);
        if (!cif)
          break;
        const char *key = esp_netif_get_ifkey(cif);
        if (key && !strcmp("WIFI_AP_DEF", key)) 
          continue;
        if (esp_netif_is_netif_up(cif))
          return true;
      }
      return false;
    }

    bool isDnsResponding() {
      const ip_addr_t *dns = dns_getserver(0);
      if (!dns)
        return false;
      return isAnyNetifUp();
    }

    struct {
      bool netif : 1;
      bool loop : 1;
    } inited = {};

    esp_err_t useNetif() {
      if (!inited.netif) {
        ESP_CHECK_RETURN(esp_netif_init());
        inited.netif = true;
      }
      return ESP_OK;
    }

    bool isNetifInited() {
      return inited.netif;
    }

    esp_err_t useEventLoop() {
      if (!inited.loop) {
        ESP_CHECK_RETURN(esp_event_loop_create_default());
        inited.loop = true;
      }
      return ESP_OK;
    }

    bool isEventLoopInited() {
      return inited.loop;
    }

  }  // namespace net
}  // namespace esp32m