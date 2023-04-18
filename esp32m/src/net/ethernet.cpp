#include "esp32m/net/ethernet.hpp"
#include "esp32m/base.hpp"
#include "esp32m/io/gpio.hpp"
#include "esp32m/net/ip_event.hpp"
#include "esp32m/net/net.hpp"

#include <esp_event.h>
#include <esp_mac.h>

namespace esp32m {
  namespace net {

    const char *EthEvent::NAME = "ethernet";

    bool EthEvent::is(eth_event_t event) const {
      return _event == event;
    }

    bool EthEvent::is(Event &ev, EthEvent **r) {
      if (!ev.is(NAME))
        return false;
      if (r)
        *r = (EthEvent *)&ev;
      return true;
    }

    bool EthEvent::is(Event &ev, eth_event_t event, EthEvent **r) {
      if (!ev.is(NAME))
        return false;
      eth_event_t t = ((EthEvent &)ev)._event;
      if (t != event)
        return false;
      if (r)
        *r = (EthEvent *)&ev;
      return true;
    }

    void EthEvent::publish(eth_event_t event, esp_eth_handle_t handle) {
      EthEvent ev(event, handle);
      EventManager::instance().publish(ev);
    }

    void eth_event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data) {
      EthEvent::publish((eth_event_t)event_id, *(esp_eth_handle_t *)event_data);
    }

    bool ethEventHandlerInstalled = false;

    esp_err_t ensureEventHandler() {
      if (!ethEventHandlerInstalled) {
        ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                                   &eth_event_handler, NULL));
        ethEventHandlerInstalled = true;
      }
      return ESP_OK;
    }

    void eth_ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
      IpEvent::publish((ip_event_t)event_id, event_data);
    }

    Ethernet::Ethernet(const char *name, esp_eth_config_t &config)
        : _name(name ? name : "eth"), _config(config) {}

    Ethernet::~Ethernet() {
      stop();
    }

    esp_err_t Ethernet::ensureReady() {
      if (!_ready) {
        ESP_CHECK_RETURN(useNetif());
        ESP_CHECK_RETURN(useEventLoop());
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        _if = esp_netif_new(&cfg);
        ESP_CHECK_RETURN(esp_eth_driver_install(&_config, &_handle));
        ESP_CHECK_RETURN(
            esp_netif_attach(_if, esp_eth_new_netif_glue(_handle)));
        ESP_CHECK_RETURN(ensureEventHandler());
        ESP_CHECK_RETURN(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_ETH_GOT_IP, eth_ip_event_handler, this,
            &_gotIpHandle));
        ESP_CHECK_RETURN(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_ETH_LOST_IP, eth_ip_event_handler, this,
            &_lostIpHandle));
        ESP_CHECK_RETURN(esp_eth_start(_handle));
        _ready = true;
        updateHostname();
      }
      return ESP_OK;
    }
    esp_err_t Ethernet::stop() {
      if (_handle) {
        if (_ready)
          ESP_CHECK_RETURN(esp_eth_stop(_handle));
        ESP_CHECK_RETURN(esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_ETH_GOT_IP, _gotIpHandle));
        _gotIpHandle = nullptr;
        ESP_CHECK_RETURN(esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_ETH_LOST_IP, _lostIpHandle));
        _lostIpHandle = nullptr;
        ESP_CHECK_RETURN(esp_eth_driver_uninstall(_handle));
        _handle = nullptr;
      }
      if (_if) {
        esp_netif_destroy(_if);
        _if = nullptr;
      }
      _ready = false;
      return ESP_OK;
    }

    bool Ethernet::handleEvent(Event &ev) {
      EthEvent *eth;
      IpEvent *ip;
      if (EthEvent::is(ev, &eth) && eth->handle() == _handle) {
        eth->claim(this);
        switch (eth->event()) {
          case ETHERNET_EVENT_CONNECTED:
            esp_eth_ioctl(_handle, ETH_CMD_G_MAC_ADDR, _macAddr);
            logI("link up, HW addr: " MACSTR, MAC2STR(_macAddr));
            break;
          case ETHERNET_EVENT_DISCONNECTED:
            logI("link down");
            break;
          case ETHERNET_EVENT_START:
            logI("started");
            break;
          case ETHERNET_EVENT_STOP:
            logI("stopped");
            break;
          default:
            break;
        }
        return true;
      } else if (IpEvent::is(ev, &ip))
        switch (ip->event()) {
          case IP_EVENT_ETH_GOT_IP: {
            ip_event_got_ip_t *d = (ip_event_got_ip_t *)ip->data();
            if (d->esp_netif != _if)
              break;
            const esp_netif_ip_info_t *ip_info = &d->ip_info;
            logI("my IP: " IPSTR, IP2STR(&ip_info->ip));
            break;
          }
          case IP_EVENT_ETH_LOST_IP: {
            ip_event_got_ip_t *d = (ip_event_got_ip_t *)ip->data();
            if (d->esp_netif != _if)
              break;
            logI("lost IP");
            break;
          }
          default:
            break;
        }
      else if (EventInit::is(ev, 0)) {
        ensureReady();
        return true;
      } else if (EventPropChanged::is(ev, "app", "hostname")) {
        updateHostname();
      }
      return false;
    }

    esp_err_t Ethernet::updateHostname() {
      if (_ready) {
        const char *hostname = App::instance().hostname();
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_hostname(_if, hostname));
      }
      return ESP_OK;
    }

#if CONFIG_ETH_USE_ESP32_EMAC
    Ethernet *useOlimexEthernet(const char *name) {
      // power up LAN8710 chip
      auto pin = gpio::pin(GPIO_NUM_12);
      pin->setDirection(GPIO_MODE_OUTPUT);
      pin->digitalWrite(true);
      delay(10);

      eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
      eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
      emac_config.smi_mdc_gpio_num = 23;
      emac_config.smi_mdio_gpio_num = 18;
      esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
      eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
      phy_config.phy_addr = 0;
      phy_config.reset_gpio_num = -1;
      esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
      esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
      auto ether = new Ethernet(name, config);

      return ether;
    }
#endif
  }  // namespace net
}  // namespace esp32m