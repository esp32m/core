#pragma once

#include <ArduinoJson.h>
#include <dhcpserver/dhcpserver.h>
#include <dhcpserver/dhcpserver_options.h>
#include <esp_err.h>
#include <esp_netif.h>
#include <lwip/ip_addr.h>
#include <map>
#include <vector>

#include "esp32m/errors.hpp"
#include "esp32m/events.hpp"

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
    void macTo(JsonObject target, const char *key, const uint8_t mac[]);
    void macTo(JsonObject target, const uint8_t mac[]);
    bool macTo(JsonArray target, const uint8_t mac[]);
    void interfacesTo(JsonObject target);
    size_t interfacesSize();

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

    constexpr const char *ErrNoInternet = "ERR_NO_INTERNET";

    const int Ipv4MaxChars = 16;
    const int IpInfoJsonSize =
        JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(3) + (Ipv4MaxChars * 3);
    const int DhcpsLeaseJsonSize =
        JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(3) + (Ipv4MaxChars * 2);
    const int Ipv6MaxChars = 40;
    const int MacMaxChars = 18;

    enum class IfEventType {
      Created,  // new interface created
    };

    class IfEvent : public Event {
     public:
      IfEventType event() const {
        return _event;
      }
      const char *key() const {
        return _key;
      }

      bool is(IfEventType event) const {
        return _event == event;
      }

      static bool is(Event &ev, IfEvent **r) {
        auto result = ev.is(Type);
        if (result && r)
          *r = (IfEvent *)&ev;
        return result;
      }
      static bool is(Event &ev, IfEventType event, IfEvent **r = nullptr) {
        auto result = ev.is(Type);
        if (result) {
          auto ifev = (IfEvent *)&ev;
          result = ifev->is(event);
          if (result && r)
            *r = ifev;
        }
        return result;
      }

      static void publish(const char *key, IfEventType event) {
        IfEvent evt(key, event);
        evt.Event::publish();
      }
      static void publish(esp_netif_t *netif, IfEventType event) {
        IfEvent evt(esp_netif_get_ifkey(netif), event);
        evt.Event::publish();
      }

     private:
      const char *_key;
      IfEventType _event;
      IfEvent(const char *key, IfEventType event)
          : Event(Type), _key(key), _event(event) {}
      constexpr static const char *Type = "net-if";
    };

    bool isEmptyMac(uint8_t *mac);
    bool isEmpty(esp_netif_dns_info_t *dns);
    bool macParse(const char *macstr, uint8_t target[]);

    void netifEnum(std::vector<esp_netif_t *> &netifs);

    bool isAnyNetifUp();
    bool isDnsAvailable();

    esp_err_t useNetif();
    bool isNetifInited();
    esp_err_t useEventLoop();
    bool isEventLoopInited();

  }  // namespace net
}  // namespace esp32m