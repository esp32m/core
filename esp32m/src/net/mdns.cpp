#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp32m/app.hpp"
#include "esp32m/net/net.hpp"
#include "esp32m/net/mdns.hpp"

namespace esp32m {
  namespace net {

    namespace mdns {

      void Service::sync() {
        auto ap = App::instance().props().get();
        mdns_txt_item_t *items = (mdns_txt_item_t *)calloc(
            (ap.size() + _props.size()), sizeof(mdns_txt_item_t));
        int i = 0;
        for (auto &p : _props) {
          auto &item = items[i++];
          item.key = p.first.c_str();
          item.value = p.second.c_str();
        }
        for (auto &p : ap) {
          auto &item = items[i++];
          item.key = p.first.c_str();
          item.value = p.second.c_str();
        }
        if (!mdns_service_exists(_type, _proto, nullptr)) {
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              mdns_service_add(nullptr, _type, _proto, _port, items, i));
        } else {
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              mdns_service_txt_set(_type, _proto, items, i));
        }
        free(items);
      }

    }  // namespace mdns

    void Mdns::handleEvent(Event &ev) {
      if (IpEvent::is(ev, IP_EVENT_STA_GOT_IP, nullptr)) {
        if (!_initialized) {
          _initialized = ESP_ERROR_CHECK_WITHOUT_ABORT(mdns_init()) == ESP_OK;
          updateHostname();
        }
        if (_initialized) {
          mdns::EventPopulate ev;
          EventManager::instance().publish(ev);
          updateServices();
        }
      } else if (EventPropChanged::is(ev, "app", "hostname")) {
        updateHostname();
      }
    }

    void Mdns::updateHostname() {
      if (!_initialized)
        return;
      const char *hostname = App::instance().hostname();
      ESP_ERROR_CHECK_WITHOUT_ABORT(mdns_hostname_set(hostname));
    }

    void Mdns::updateServices() {
      if (!_initialized)
        return;
      for (auto &kv : _services) {
        kv.second->sync();
      }
    }

    Mdns &Mdns::instance() {
      static Mdns i;
      return i;
    }

    Mdns *useMdns() {
      return &Mdns::instance();
    }
  }  // namespace net
}  // namespace esp32m
