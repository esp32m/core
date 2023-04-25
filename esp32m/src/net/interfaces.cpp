#include "esp32m/net/interfaces.hpp"

#include "esp32m/json.hpp"
#include "esp32m/net/net.hpp"

namespace esp32m {
  namespace net {

    Interface::Interface() {}
    Interface::~Interface() {}

    void Interface::init(const char *key) {
      _key = key;
      Interfaces::instance().reg(this);
      ErrorList el;
      syncHandle(el);
    }

    esp_err_t Interface::getIpInfo(esp_netif_ip_info_t &info) {
      return esp_netif_get_ip_info(_handle, &info);
    }

    bool Interface::isUp() {
      return esp_netif_is_netif_up(_handle);
    }

    void Interface::syncHandle(ErrorList &errl) {
      _handle = esp_netif_get_handle_from_ifkey(_key.c_str());
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
      esp_netif_dhcpc_stop(_handle);
      esp_netif_dhcps_stop(_handle);
    }

    void Interface::apply(ConfigItem item, ErrorList &errl) {
      if (!_handle)
        return;
      if (_role == Role::Default)
        return;
      switch (item) {
        case ConfigItem::Role:
          switch (_role) {
            case Role::Static:
              esp_netif_dhcpc_stop(_handle);
              esp_netif_dhcps_stop(_handle);
              break;
            case Role::DhcpClient:
              esp_netif_dhcps_stop(_handle);
              esp_netif_dhcpc_start(_handle);
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
              logI("set DNS: %d, %x", kv.first, kv.second.ip.u_addr.ip4.addr);
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

    DynamicJsonDocument *Interface::getState(const JsonVariantConst args) {
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
        auto size =
            IpInfoJsonSize +
            JSON_OBJECT_SIZE(2 /*up, prio*/ + (desc ? 1 : 0) +
                             (hostname ? 1 : 0) + (hasDhcpc ? 1 : 0) +
                             (hasDhcps ? 1 : 0)) +
            (hasMac ? (JSON_OBJECT_SIZE(1) + MacMaxChars) : 0) +
            (ip6count ? (JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(ip6count) +
                         ip6count * Ipv6MaxChars)
                      : 0) +
            (dnsCount ? (JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(3) +
                         (dnsCount * Ipv6MaxChars))
                      : 0);

        auto doc = new DynamicJsonDocument(size);
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
    bool Interface::setConfig(const JsonVariantConst cfg,
                              DynamicJsonDocument **result) {
      ErrorList el;
      bool changed = false;
      JsonObjectConst root = cfg.as<JsonObjectConst>();
      if (json::fromIntCastable(root["role"], _role, &changed))
        apply(ConfigItem::Role, el);
      if (json::macFrom(root["mac"], _mac, &changed))
        apply(ConfigItem::Mac, el);
      if (json::from(root, _ip, &changed))
        apply(ConfigItem::Ip, el);
      if (json::from(root, _ipv6, &changed))
        apply(ConfigItem::Ipv6, el);
      if (json::from(root, _dns, &changed))
        apply(ConfigItem::Dns, el);
      auto dhcps = root["dhcps"].as<JsonObjectConst>();
      if (dhcps)
        if (json::from(dhcps, _dhcpsLease, &changed))
          apply(ConfigItem::DhcpsLease, el);
      el.toJson(result);
      _configLoaded = true;
      return changed;
    }
    DynamicJsonDocument *Interface::getConfig(RequestContext &ctx) {
      auto hasIp = _ip.ip.addr || _ip.gw.addr || _ip.netmask.addr;
      auto hasMac = !isEmptyMac(_mac);
      auto ip6count = _ipv6.size();
      auto dnsCount = _dns.size();
      auto hasLease = _dhcpsLease.start_ip.addr || _dhcpsLease.end_ip.addr;
      auto hasDhcps = hasLease;
      auto size = JSON_OBJECT_SIZE(1 /*role*/ + (hasDhcps ? 1 : 0)) +
                  (hasIp ? IpInfoJsonSize : 0) +
                  (hasMac ? (JSON_OBJECT_SIZE(1) + MacMaxChars) : 0) +
                  (ip6count ? (JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(ip6count) +
                               (ip6count * Ipv6MaxChars))
                            : 0) +
                  (dnsCount ? (JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(3) +
                               (dnsCount * Ipv6MaxChars))
                            : 0) +
                  (hasLease ? DhcpsLeaseJsonSize : 0);
      auto doc = new DynamicJsonDocument(size);
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
        auto dhcps = root.createNestedObject("dhcps");
        if (hasLease)
          json::to(dhcps, _dhcpsLease);
      }
      return doc;
    }

    Interfaces::~Interfaces() {
      std::lock_guard guard(_mapMutex);
      for (const auto &i : _map) delete i.second;
    }
    DynamicJsonDocument *Interfaces::getState(const JsonVariantConst args) {
      syncMap();
      json::ConcatToObject c;
      for (const auto &i : _map) {
        auto state = i.second->getState(args);
        if (state) {
          json::check(this, state, "getState()");
          c.add(i.first.c_str(), state);
        }
      }
      return c.concat();
    }
    bool Interfaces::setConfig(const JsonVariantConst cfg,
                               DynamicJsonDocument **result) {
      bool changed = false;
      JsonObjectConst map = cfg.as<JsonObjectConst>();
      for (JsonPairConst kv : map) {
        // logi("setConfig for %s", kv.key().c_str());
        if (getOrAddInterface(kv.key().c_str())->setConfig(kv.value(), result))
          changed = true;
      }
      return changed;
    }
    DynamicJsonDocument *Interfaces::getConfig(RequestContext &ctx) {
      json::ConcatToObject c;
      for (const auto &i : _map) {
        auto config = i.second->getConfig(ctx);
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
      ErrorList el;
      if (!_mapValid || esp_netif_get_nr_of_ifs() != c) {
        esp_netif_t *cif = nullptr;
        int ac = 0;
        for (;;) {
          cif = esp_netif_next(cif);
          if (!cif)
            break;
          const char *key = esp_netif_get_ifkey(cif);
          getOrAddInterface(key)->syncHandle(el);
          ac++;
        }
        logI("%d configured, %d active", _map.size(), ac);
        _mapValid = true;
      }
    }
    Interface *Interfaces::getOrAddInterface(const char *key) {
      {
        std::lock_guard guard(_mapMutex);
        auto i = _map.find(key);
        if (i != _map.end())
          return i->second;
      }
      auto newIface = new Interface();
      newIface->init(key);
      return newIface;
    }

    void Interfaces::reg(Interface *i) {
      auto key = i->key();
      std::lock_guard guard(_mapMutex);
      auto old = _map.find(key);
      if (old != _map.end() && old->second) {
        logw("deleting orphaned interface %s", key.c_str());
        if (!old->second->_persistent)
          delete old->second;
        old->second = i;
      } else
        _map[key] = i;
    }

    Interfaces &useInterfaces() {
      return Interfaces::instance();
    }

  }  // namespace net
}  // namespace esp32m