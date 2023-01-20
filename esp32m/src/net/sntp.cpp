// need this as of 2023-01-20 to compile apps/esp_sntp.h
#define ESP_LWIP_COMPONENT_BUILD

#include "apps/esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp32m/json.hpp"
#include "esp32m/net/ip_event.hpp"
#include "esp32m/net/sntp.hpp"
#include "esp32m/net/net.hpp"

namespace esp32m {
  namespace net {

    const char *DefaultNtpHost = "pool.ntp.org";

    bool Sntp::handleEvent(Event &ev) {
      if (IpEvent::is(ev, IP_EVENT_STA_GOT_IP, nullptr))
        xTimerPendFunctionCall(
            [](void *self, uint32_t a) { ((Sntp *)self)->update(); }, this, 0,
            pdMS_TO_TICKS(1000));
      else
        return false;
      return true;
    }

    DynamicJsonDocument *Sntp::getState(const JsonVariantConst args) {
      size_t size = JSON_OBJECT_SIZE(2);
      auto doc = new DynamicJsonDocument(size);
      auto root = doc->to<JsonObject>();
      json::to(root, "status", (int)sntp_get_sync_status());
      time_t t;
      time(&t);
      json::to(root, "time", t);
      return doc;
    }

    DynamicJsonDocument *Sntp::getConfig(const JsonVariantConst args) {
      size_t size = JSON_OBJECT_SIZE(4 /*enabled, host, tz, dst*/) +
                    JSON_STRING_SIZE(_host.size());
      auto doc = new DynamicJsonDocument(size);
      auto root = doc->to<JsonObject>();
      json::to(root, "enabled", _enabled);
      if (_host.size())
        json::to(root, "host", _host);
      else
        json::to(root, "host", DefaultNtpHost);
      if (_tzOfs)
        json::to(root, "tz", _tzOfs);
      if (_dstOfs)
        json::to(root, "dst", _dstOfs);
      return doc;
    }

    bool Sntp::setConfig(const JsonVariantConst data,
                         DynamicJsonDocument **result) {
      JsonObjectConst obj = data.as<JsonObjectConst>();
      bool changed = false;
      json::from(obj["enabled"], _enabled, &changed);
      json::from(obj["host"], _host, &changed);
      if (!_host.size()) {
        _host = DefaultNtpHost;
        changed = true;
      }
      json::from(obj["tz"], _tzOfs, &changed);
      json::from(obj["dst"], _dstOfs, &changed);
      if (changed)
        update();
      return changed;
    }

    void Sntp::update() {
      if (!net::isNetifInited())
        return;
      esp_sntp_stop();
      if (_enabled) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0,
                               _host.size() ? _host.c_str() : DefaultNtpHost);
        esp_sntp_init();
        char cst[17] = {0};
        char cdt[17] = "DST";
        char tz[33] = {0};
        long offset = _tzOfs * 60;
        int daylight = _dstOfs * 60;
        if (offset % 3600)
          sprintf(cst, "UTC%ld:%02u:%02u", (offset / 3600),
                  (unsigned int)abs((offset % 3600) / 60),
                  (unsigned int)abs(offset % 60));
        else
          sprintf(cst, "UTC%ld", offset / 3600);
        if (daylight != 3600) {
          long tz_dst = offset - daylight;
          if (tz_dst % 3600)
            sprintf(cdt, "DST%ld:%02u:%02u", tz_dst / 3600,
                    (unsigned int)abs((tz_dst % 3600) / 60),
                    (unsigned int)abs(tz_dst % 60));
          else
            sprintf(cdt, "DST%ld", tz_dst / 3600);
        }
        sprintf(tz, "%s%s", cst, cdt);
        setenv("TZ", tz, 1);
        tzset();
      }
    }

    Sntp &Sntp::instance() {
      static Sntp i;
      return i;
    }

    Sntp *useSntp() {
      return &Sntp::instance();
    }
  }  // namespace net
}  // namespace esp32m