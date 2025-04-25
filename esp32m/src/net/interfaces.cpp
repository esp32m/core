#include "esp32m/net/interfaces.hpp"

#include "esp32m/json.hpp"
#include "esp32m/net/net.hpp"

#include <esp_event.h>
#include <sdkconfig.h>
#if CONFIG_LWIP_IPV6_DHCP6
#  include <esp_netif_net_stack.h>
#  include <lwip/dhcp6.h>
#endif

namespace esp32m {
  namespace net {

    Interface::Interface() {}
    Interface::~Interface() {}

    void Interface::init(const char *key) {
      if (_handle)
        return;
      _key = key;
      _handle = esp_netif_get_handle_from_ifkey(_key.c_str());
      Interfaces::instance().reg(this);
      ErrorList el;
      syncHandle(el);
    }

    esp_err_t Interface::getIpInfo(esp_netif_ip_info_t &info) {
      if (!_handle)
        return ESP_FAIL;
      return esp_netif_get_ip_info(_handle, &info);
    }

    bool Interface::isUp() {
      return _handle && esp_netif_is_netif_up(_handle);
    }

    void Interface::syncHandle(ErrorList &errl) {
      if (_handle) {
        if (!_configLoaded) {
          errl.check(esp_netif_get_mac(_handle, _mac));
          esp_netif_dhcp_status_t dhcpc;
          auto hasDhcpc = esp_netif_dhcpc_get_status(_handle, &dhcpc) == ESP_OK;
          if (!hasDhcpc || dhcpc == ESP_NETIF_DHCP_STOPPED) {
            errl.check(esp_netif_get_ip_info(_handle, &_ip));
            esp_ip6_addr_t ip6[LWIP_IPV6_NUM_ADDRESSES];
            auto ip6count = esp_netif_get_all_ip6(_handle, ip6);
            _ipv6.clear();
            for (auto i = 0; i < ip6count; i++) _ipv6.push_back(ip6[i]);
            _dns.clear();
            esp_netif_dns_info_t dns;
            for (auto i = 0; i < ESP_NETIF_DNS_MAX; i++) {
              if (errl.check(esp_netif_get_dns_info(
                      _handle, (esp_netif_dns_type_t)i, &dns)) == ESP_OK &&
                  !isEmpty(&dns))
                _dns[(esp_netif_dns_type_t)i] = dns;
            }
          }
        } else
          apply(errl);
      }
    }

    void Interface::apply(ErrorList &errl) {
      stopDhcp();
      for (auto i = 0; i < (int)ConfigItem::MAX; i++)
        apply((ConfigItem)i, errl);
    }

    void Interface::stopDhcp() {
      if (_handle) {
        esp_netif_dhcpc_stop(_handle);
        esp_netif_dhcps_stop(_handle);
      }
    }

    void Interface::apply(ConfigItem item, ErrorList &errl) {
      if (!_handle)
        return;
      if (_role == Role::Default)
        return;
#if CONFIG_LWIP_IPV6_DHCP6
      auto lwip_netif = (struct netif *)esp_netif_get_netif_impl(_handle);
#endif
      switch (item) {
        case ConfigItem::Role:
          switch (_role) {
            case Role::Static:
              esp_netif_dhcpc_stop(_handle);
              esp_netif_dhcps_stop(_handle);
#if CONFIG_LWIP_IPV6_DHCP6
              dhcp6_disable(lwip_netif);
#endif
              break;
            case Role::DhcpClient:
              esp_netif_dhcps_stop(_handle);
              esp_netif_dhcpc_start(_handle);
#if CONFIG_LWIP_IPV6_DHCP6
              errl.check(dhcp6_enable_stateless(lwip_netif));
#endif
              break;
            case Role::DhcpServer: {
              esp_netif_dhcpc_stop(_handle);
              errl.check(esp_netif_dhcps_start(_handle));
              break;
            }
            default:
              break;
          }
          break;
        case ConfigItem::Mac:
          if (!isEmptyMac(_mac))
            errl.check(esp_netif_set_mac(_handle, _mac));
          break;
        case ConfigItem::Ip:
          if (_role != Role::DhcpClient)
            errl.check(esp_netif_set_ip_info(_handle, &_ip));
          break;
        case ConfigItem::Ipv6:
          if (_role != Role::DhcpClient) {
            esp_ip6_addr_t ip6[LWIP_IPV6_NUM_ADDRESSES];
            auto ip6count = esp_netif_get_all_ip6(_handle, ip6);
            std::vector<esp_ip6_addr> vec(_ipv6);
            for (auto i = 0; i < ip6count; i++) {
              bool found = false;
              for (auto it = vec.begin(); it != vec.end();) {
                if (!memcmp(&(*it), &ip6[i], sizeof(esp_ip6_addr_t))) {
                  vec.erase(it);
                  found = true;
                  break;
                }
              }
              if (!found)
                esp_netif_action_remove_ip6_address(_handle, IP_EVENT, 0,
                                                    &ip6[i]);
            }
            for (auto &v6 : vec)
              esp_netif_action_add_ip6_address(_handle, IP_EVENT, 0, &v6);
          }
          break;
        case ConfigItem::Dns:
          if (_role != Role::DhcpClient)
            for (auto &kv : _dns) {
              // only set primary DNS for DhcpServer
              if (_role == Role::DhcpServer && kv.first > ESP_NETIF_DNS_MAIN)
                break;
              // logI("set DNS: %d, %x", kv.first,
              // kv.second.ip.u_addr.ip4.addr);
              errl.check(esp_netif_set_dns_info(_handle, kv.first, &kv.second));
            }
          if (_role == Role::DhcpServer) {
            dhcps_offer_t offer = OFFER_DNS;
            errl.check(esp_netif_dhcps_option(_handle, ESP_NETIF_OP_SET,
                                              ESP_NETIF_DOMAIN_NAME_SERVER,
                                              &offer, sizeof(offer)));
          }
          break;
        case ConfigItem::DhcpsLease:
          if (_role == Role::DhcpServer) {
            errl.check(esp_netif_dhcps_option(
                _handle, ESP_NETIF_OP_SET, ESP_NETIF_REQUESTED_IP_ADDRESS,
                &_dhcpsLease, sizeof(dhcps_lease_t)));
          }
          break;
        case ConfigItem::Hostname: {
          const char *hostname = App::instance().hostname();
          errl.check(esp_netif_set_hostname(_handle, hostname));
        } break;
        default:
          break;
      }
    }

    JsonDocument *Interface::getState(RequestContext &ctx) {
      if (_handle) {
        uint8_t mac[6];
        bool hasMac = esp_netif_get_mac(_handle, mac) == ESP_OK;
        const char *hostname = nullptr;
        esp_netif_get_hostname(_handle, &hostname);
        esp_ip6_addr_t ip6[LWIP_IPV6_NUM_ADDRESSES];
        auto ip6count = esp_netif_get_all_ip6(_handle, ip6);
        auto desc = esp_netif_get_desc(_handle);
        esp_netif_dhcp_status_t dhcpc, dhcps;
        auto hasDhcpc = esp_netif_dhcpc_get_status(_handle, &dhcpc) == ESP_OK;
        auto hasDhcps = esp_netif_dhcps_get_status(_handle, &dhcps) == ESP_OK;
        std::map<esp_netif_dns_type_t, esp_netif_dns_info_t> dns;
        esp_netif_dns_info_t dnsItem;
        for (auto i = 0; i < ESP_NETIF_DNS_MAX; i++)
          if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_get_dns_info(
                  _handle, (esp_netif_dns_type_t)i, &dnsItem)) == ESP_OK)
            if (!isEmpty(&dnsItem))
              dns[(esp_netif_dns_type_t)i] = dnsItem;

        auto dnsCount = dns.size();
        /*auto size =
            IpInfoJsonSize +
            JSON_OBJECT_SIZE(2 + (desc ? 1 : 0) +
                             (hostname ? 1 : 0) + (hasDhcpc ? 1 : 0) +
                             (hasDhcps ? 1 : 0)) +
            (hasMac ? (JSON_OBJECT_SIZE(1) + MacMaxChars) : 0) +
            (ip6count ? (JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(ip6count) +
                         ip6count * Ipv6MaxChars)
                      : 0) +
            (dnsCount ? (JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(3) +
                         (dnsCount * Ipv6MaxChars))
                      : 0);*/

        auto doc = new JsonDocument(); /* size */
        auto root = doc->to<JsonObject>();
        root["up"] = isUp();
        if (desc)
          root["desc"] = desc;
        if (hasMac)
          json::macTo(root, mac);
        if (hostname)
          root["hostname"] = hostname;
        esp_netif_ip_info_t ip;
        if (!ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_get_ip_info(_handle, &ip)))
          json::to(root, ip);
        if (ip6count)
          json::to(root, ip6, ip6count);
        if (hasDhcpc)
          json::to(root, "dhcpc", dhcpc);
        if (hasDhcps)
          json::to(root, "dhcps", dhcps);
        if (dnsCount)
          json::to(root, dns);
        root["prio"] = esp_netif_get_route_prio(_handle);
        return doc;
      }
      return nullptr;
    }
    bool Interface::setConfig(RequestContext &ctx) {
      bool changed = false;
      //      json::dump(this, cfg, "setConfig");
      JsonObjectConst root = ctx.data.as<JsonObjectConst>();
      if (json::fromIntCastable(root["role"], _role, &changed))
        apply(ConfigItem::Role, ctx.errors);
      if (json::macFrom(root["mac"], _mac, &changed))
        apply(ConfigItem::Mac, ctx.errors);
      if (json::from(root, _ip, &changed))
        apply(ConfigItem::Ip, ctx.errors);
      if (json::from(root, _ipv6, &changed))
        apply(ConfigItem::Ipv6, ctx.errors);
      if (json::from(root, _dns, &changed))
        apply(ConfigItem::Dns, ctx.errors);
      auto dhcps = root["dhcps"].as<JsonObjectConst>();
      if (dhcps)
        if (json::from(dhcps, _dhcpsLease, &changed))
          apply(ConfigItem::DhcpsLease, ctx.errors);
      _configLoaded = true;
      return changed;
    }
    JsonDocument *Interface::getConfig() {
      auto hasIp = _ip.ip.addr || _ip.gw.addr || _ip.netmask.addr;
      auto hasMac = !isEmptyMac(_mac);
      auto ip6count = _ipv6.size();
      auto dnsCount = _dns.size();
      auto hasLease = _dhcpsLease.start_ip.addr || _dhcpsLease.end_ip.addr;
      auto hasDhcps = hasLease;
      /*auto size = JSON_OBJECT_SIZE(1  + (hasDhcps ? 1 : 0)) +
                  (hasIp ? IpInfoJsonSize : 0) +
                  (hasMac ? (JSON_OBJECT_SIZE(1) + MacMaxChars) : 0) +
                  (ip6count ? (JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(ip6count) +
                               (ip6count * Ipv6MaxChars))
                            : 0) +
                  (dnsCount ? (JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(3) +
                               (dnsCount * Ipv6MaxChars))
                            : 0) +
                  (hasLease ? DhcpsLeaseJsonSize : 0);*/
      auto doc = new JsonDocument(); /* size */
      auto root = doc->to<JsonObject>();
      root["role"] = (int)_role;
      if (hasMac)
        json::macTo(root, _mac);
      if (hasIp)
        json::to(root, _ip);
      if (ip6count)
        json::to(root, &_ipv6[0], ip6count);
      if (dnsCount)
        json::to(root, _dns);
      if (hasDhcps) {
        auto dhcps = root["dhcps"].to<JsonObject>();
        if (hasLease)
          json::to(dhcps, _dhcpsLease);
      }
      return doc;
    }

    Interfaces::~Interfaces() {
      if (_gotIp6Handle) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_ETH_GOT_IP, _gotIp6Handle));
        _gotIp6Handle = nullptr;
      }
      std::lock_guard guard(_mapMutex);
      for (const auto &i : _map) delete i.second;
    }

    void got_ip6_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
      auto interfaces = (Interfaces *)arg;
      auto ip6ev = (ip_event_got_ip6_t *)event_data;
      if (ip6ev && ip6ev->esp_netif) {
        IpEvent::publish(ip6ev->esp_netif, (ip_event_t)event_id, event_data);
        const char *key = esp_netif_get_ifkey(ip6ev->esp_netif);
        auto iface = interfaces->findInterface(key);
        if (iface)
          iface->logger().logf(log::Level::Info, "got IPv6: " IPV6STR,
                               IPV62STR(ip6ev->ip6_info.ip));
      }
    }

    void Interfaces::handleEvent(Event &ev) {
      union {
        net::IfEvent *ifev;
        net::IpEvent *ipev;
      };
      if (EventInit::is(ev, 0)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_GOT_IP6, got_ip6_event_handler, this,
            &_gotIp6Handle));
      } else if (net::IfEvent::is(ev, net::IfEventType::Created, &ifev)) {
        getOrAddInterface(ifev->key());
      } else if (net::IpEvent::is(ev, &ipev)) {
        const char *key = esp_netif_get_ifkey(ipev->netif());
        auto iface = findInterface(key);
        if (iface)
          switch (ipev->kind()) {
            case IpEventKind::GotIpv4: {
              auto *d = (ip_event_got_ip_t *)ipev->data();
              const esp_netif_ip_info_t *ip_info = &d->ip_info;
              iface->logger().logf(log::Level::Info, "got IPv4: " IPSTR,
                                   IP2STR(&ip_info->ip));
            } break;
            case IpEventKind::LostIpv4:
              iface->logger().log(log::Level::Info, "lost IPv4");
              break;
            default:
              break;
          }
      }
    }
    JsonDocument *Interfaces::getState(RequestContext &ctx) {
      syncMap();
      json::ConcatToObject c;
      for (const auto &i : _map) {
        auto state = i.second->getState(ctx);
        if (state) {
          json::check(this, state, "getState()");
          c.add(i.first.c_str(), state);
        }
      }
      return c.concat();
    }
    bool Interfaces::setConfig(RequestContext &ctx) {
      bool changed = false;
      JsonObjectConst map = ctx.data.as<JsonObjectConst>();
      for (JsonPairConst kv : map) {
        // logi("setConfig for %s", kv.key().c_str());
        RequestContext ic(ctx.request, kv.value());
        if (getOrAddInterface(kv.key().c_str())->setConfig(ic))
          changed = true;
      }
      return changed;
    }
    JsonDocument *Interfaces::getConfig(RequestContext &ctx) {
      json::ConcatToObject c;
      for (const auto &i : _map) {
        auto config = i.second->getConfig();
        if (config) {
          json::check(this, config, "getConfig()");
          c.add(i.first.c_str(), config);
        }
      }
      return c.concat();
    }

    Interfaces &Interfaces::instance() {
      static Interfaces i;
      return i;
    }

    void Interfaces::syncMap() {
      int c = 0;
      {
        std::lock_guard guard(_mapMutex);
        for (const auto &i : _map)
          if (i.second->handle())
            c++;
      }
      // ErrorList el;
      if (!_mapValid || esp_netif_get_nr_of_ifs() != c) {
        std::vector<esp_netif_t *> netifs;
        netifEnum(netifs);
        int ac = 0;
        for (auto cif : netifs) {
          const char *key = esp_netif_get_ifkey(cif);
          getOrAddInterface(key);
          ac++;
        }

        /*        esp_netif_t *cif = nullptr;
                for (;;) {
                  cif = esp_netif_next_unsafe(cif);
                  if (!cif)
                    break;
                }*/
        logI("%d configured, %d active", _map.size(), ac);
        _mapValid = true;
      }
    }
    Interface *Interfaces::findInterface(const char *key) {
      Interface *result = nullptr;
      if (key) {
        std::lock_guard guard(_mapMutex);
        auto i = _map.find(key);
        if (i != _map.end())
          result = i->second;
      }
      return result;
    }
    Interface *Interfaces::getOrAddInterface(const char *key) {
      Interface *result = findInterface(key);
      if (key) {
        if (!result)
          result = new Interface();
        result->init(key);
      }
      return result;
    }

    class DummyReq : public Request {
     public:
     DummyReq(const JsonVariantConst data)
          : Request("dummy", 0, nullptr, data, "dummy") {}

     protected:
      void respondImpl(const char *source, const JsonVariantConst data,
                       bool error) override {}

      Response *makeResponseImpl() override {
        return nullptr;
      }

     private:
    };

    void Interfaces::reg(Interface *i) {
      auto key = i->key();
      // logD("register interface %s, handle=%i", key.c_str(), i->_handle);
      std::lock_guard guard(_mapMutex);
      auto old = _map.find(key);
      if (old != _map.end() && old->second) {
        // This happens if config was loaded before the interface was created.
        // In this case we have Interface object without a _handle in the _map,
        // but with the loaded config, and we move it over to the real interfce
        // that is being added now
        if (old->second->_configLoaded && i->_handle) {
          auto doc = old->second->getConfig();
          DummyReq rq(*doc);
          RequestContext ctx(rq, *doc);
          i->setConfig(ctx);
          delete doc;
        }
        if (!old->second->_handle)
          delete old->second;
        else if (old->second !=
                 i)  // it's OK if we are registering the same interface
          logw("BUG: trying to register interface %s more than once",
               key.c_str());
        old->second = i;
      } else
        _map[key] = i;
    }

    Interfaces &useInterfaces() {
      return Interfaces::instance();
    }

  }  // namespace net
}  // namespace esp32m