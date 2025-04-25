#pragma once

#include "esp32m/app.hpp"
#include "esp32m/errors.hpp"
#include "esp32m/logging.hpp"

#include <dhcpserver/dhcpserver.h>
#include <dhcpserver/dhcpserver_options.h>
#include <esp_netif.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace esp32m {
  namespace net {

    class Interface : public virtual log::Loggable {
     public:
      enum class Role {
        Default,
        Static,
        DhcpClient,
        DhcpServer,
      };
      enum class ConfigItem {
        Mac,
        Ip,
        Ipv6,
        Dns,
        DhcpsLease,
        Hostname,
        Role,
        MAX
      };
      Interface();
      Interface(const Interface &) = delete;
      virtual ~Interface();
      const char *name() const override {
        return _key.c_str();
      }
      std::string key() const {
        return _key;
      }
      esp_netif_t *handle() {
        return _handle;
      }
      esp_err_t getIpInfo(esp_netif_ip_info_t &info);

      void apply(ConfigItem item, ErrorList &errl);
      void apply(ErrorList &errl);
      void stopDhcp();
      bool isUp();
      Role role() const {
        return _role;
      }

     protected:
      // config
      bool _configLoaded = false;
      Role _role = Role::Default;
      uint8_t _mac[6] = {};
      esp_netif_ip_info_t _ip = {};
      std::vector<esp_ip6_addr_t> _ipv6;
      std::map<esp_netif_dns_type_t, esp_netif_dns_info_t> _dns;
      dhcps_lease_t _dhcpsLease = {};
      void init(const char *key);
      JsonDocument *getState(RequestContext &ctx);
      bool setConfig(RequestContext &ctx);
      JsonDocument *getConfig();

     private:
      std::string _key;
      esp_netif_t *_handle = nullptr;
      void syncHandle(ErrorList &errl);
      friend class Interfaces;
    };

    class Interfaces : public AppObject {
     public:
      Interfaces(const Interfaces &) = delete;
      ~Interfaces();
      const char *name() const override {
        return "netifs";
      }
      static Interfaces &instance();
      Interface *findInterface(const char *key);

     protected:
      void handleEvent(Event &ev) override;
      JsonDocument *getState(RequestContext &ctx) override;
      bool setConfig(RequestContext &ctx) override;
      JsonDocument *getConfig(RequestContext &ctx) override;

     private:
      bool _mapValid = false;
      std::map<std::string, Interface *> _map;
      std::mutex _mapMutex;
      esp_event_handler_instance_t _gotIp6Handle = nullptr;
      Interfaces() {}
      void syncMap();
      Interface *getOrAddInterface(const char *key);
      void reg(Interface *i);
      friend class Interface;
    };

    Interfaces &useInterfaces();

  }  // namespace net
}  // namespace esp32m