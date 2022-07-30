#pragma once

#include "esp32m/device.hpp"
#include "esp32m/events.hpp"

#include "esp_eth.h"
#include "esp_netif.h"

namespace esp32m {
  namespace net {

    class Ethernet;

    class EthEvent : public Event {
     public:
      eth_event_t event() const {
        return _event;
      }
      esp_eth_handle_t handle() const {
        return _handle;
      }
      Ethernet *ethernet() {
        return _ethernet;
      }

      bool is(eth_event_t event) const;

      static bool is(Event &ev, EthEvent **r);
      static bool is(Event &ev, eth_event_t event, EthEvent **r = nullptr);

      static void publish(eth_event_t event, esp_eth_handle_t handle);

     protected:
      static const char *NAME;
      virtual bool handleEvent(Event &ev) {
        return false;
      };

     private:
      eth_event_t _event;
      esp_eth_handle_t _handle;
      Ethernet *_ethernet = nullptr;
      EthEvent(eth_event_t event, esp_eth_handle_t handle)
          : Event(NAME), _event(event), _handle(handle) {}
      void claim(Ethernet *e) {
        _ethernet = e;
      }
      friend class Ethernet;
    };

    class Ethernet : public Device {
     public:
      Ethernet(const Ethernet &) = delete;
      Ethernet(const char *name, esp_eth_config_t &config);
      ~Ethernet();
      const char *name() const override {
        return _name;
      }

     protected:
      bool handleEvent(Event &ev) override;

     private:
      const char *_name;
      esp_eth_config_t _config;
      uint8_t _macAddr[6] = {0};
      bool _ready = false;
      esp_netif_t *_if = nullptr;
      esp_eth_handle_t _handle = nullptr;
      esp_event_handler_instance_t _gotIpHandle = nullptr,
                                   _lostIpHandle = nullptr;
      esp_err_t ensureReady();
      esp_err_t stop();
    };

    Ethernet *useOlimexEthernet(const char *name = nullptr);

  }  // namespace net
}  // namespace esp32m