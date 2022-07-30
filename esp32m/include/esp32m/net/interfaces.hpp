#pragma once

#include "esp32m/app.hpp"

#include <dhcpserver/dhcpserver.h>
#include <dhcpserver/dhcpserver_options.h>
#include <esp_netif.h>
#include <map>
#include <mutex>
#include <vector>
#include <string>

namespace esp32m {
  namespace net {

    class Interface : public INamed {
     public:
      enum class Role {
        Default,
        Static,
        DhcpClient,
        DhcpServer,
      };
      enum class ConfigItem { Role, Mac, Ip, Ipv6, Dns, DhcpsLease, MAX = Dns };
      Interface(const char *key);
      Interface(const Interface &) = delete;
      virtual ~Interface();
      const char *name() const override {
        return _key.c_str();
      }
      esp_netif_t *handle() {
        return _handle;
      }
      void syncHandle(ErrorList &errl);
      void apply(ConfigItem item, ErrorList &errl);

      DynamicJsonDocument *getState(const JsonVariantConst args);
      bool setConfig(const JsonVariantConst cfg, DynamicJsonDocument **result);
      DynamicJsonDocument *getConfig(const JsonVariantConst args);

     private:
      std::string _key;
      esp_netif_t *_handle;
      // config
      bool _configLoaded = false;
      Role _role = Role::Default;
      uint8_t _mac[6] = {};
      esp_netif_ip_info_t _ip = {};
      std::vector<esp_ip6_addr_t> _ipv6;
      std::map<esp_netif_dns_type_t, esp_netif_dns_info_t> _dns;
      dhcps_lease_t _dhcpsLease = {};
    };

    class Interfaces : public AppObject {
     public:
      Interfaces(const Interfaces &) = delete;
      ~Interfaces();
      const char *name() const override {
        return "netifs";
      }
      static Interfaces &instance();

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;

     private:
      bool _mapValid = false;
      std::map<std::string, Interface *> _map;
      std::mutex _mapMutex;
      Interfaces() {}
      void syncMap();
      Interface *getOrAddInterface(const char *key);
    };

    Interfaces &useInterfaces();

  }  // namespace net
}  // namespace esp32m