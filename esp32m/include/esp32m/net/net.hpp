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
      Creating,  // a chance to create custom netif
      Created,   // new interface created
    };

    class IfEvent : public Event {
     public:
      IfEvent(const char *key, IfEventType event)
          : Event(Type), _key(key), _event(event) {}

      IfEventType event() const {
        return _event;
      }
      const char *key() const {
        return _key;
      }

      bool is(IfEventType event) const {
        return _event == event;
      }
      esp_netif_t *getNetif() const {
        return _netif;
      }
      void setNetif(esp_netif_t *netif) {
        _netif = netif;
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
      esp_netif_t *_netif = nullptr;
      constexpr static const char *Type = "net-if";
    };

    enum class IpEventKind { Unknown, GotIpv4, LostIpv4, GotIpv6 };

    class IpEvent : public Event {
     public:
      ip_event_t event() const {
        return _event;
      }
      void *data() const {
        return _data;
      }
      IpEventKind kind() const {
        return _kind;
      }
      bool is(ip_event_t event) const {
        return _event == event;
      }
      esp_netif_t *netif() const {
        return _netif;
      }
      static bool is(Event &ev, IpEvent **r) {
        if (!ev.is(Type))
          return false;
        if (r)
          *r = (IpEvent *)&ev;
        return true;
      }
      static bool is(Event &ev, ip_event_t event, IpEvent **r = nullptr) {
        if (!ev.is(Type))
          return false;
        ip_event_t t = ((IpEvent &)ev)._event;
        if (t != event)
          return false;
        if (r)
          *r = (IpEvent *)&ev;
        return true;
      }

      static void publish(esp_netif_t *netif, ip_event_t event, void *data) {
        IpEvent ev(netif, event, data);
        EventManager::instance().publish(ev);
      }

     protected:
      constexpr static const char *Type = "ip";

     private:
      esp_netif_t *_netif;
      ip_event_t _event;
      IpEventKind _kind = IpEventKind::Unknown;
      void *_data;
      IpEvent(esp_netif_t *netif, ip_event_t event, void *data)
          : Event(Type), _netif(netif), _event(event), _data(data) {
        if (event == IP_EVENT_GOT_IP6)
          _kind = IpEventKind::GotIpv6;
        if (event == esp_netif_get_event_id(netif, ESP_NETIF_IP_EVENT_GOT_IP))
          _kind = IpEventKind::GotIpv4;
        else if (event ==
                 esp_netif_get_event_id(netif, ESP_NETIF_IP_EVENT_LOST_IP))
          _kind = IpEventKind::LostIpv4;
      }
      friend class Wifi;
    };

    bool isEmptyMac(uint8_t *mac);
    bool isEmpty(esp_netif_dns_info_t *dns);
    bool macParse(const char *macstr, uint8_t target[]);

    void netifEnum(std::vector<esp_netif_t *> &netifs);

    bool isAnyNetifUp();
    bool isDnsAvailable();
    bool getDefaultGateway(esp_ip4_addr_t *addr);

    esp_err_t useNetif();
    bool isNetifInited();
    esp_err_t useEventLoop();
    bool isEventLoopInited();

  }  // namespace net
}  // namespace esp32m