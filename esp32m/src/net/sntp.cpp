// need this as of 2023-01-20 to compile apps/esp_sntp.h
#define ESP_LWIP_COMPONENT_BUILD

#include "apps/esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp32m/json.hpp"
#include "esp32m/net/net.hpp"
#include "esp32m/net/sntp.hpp"

namespace esp32m {
  namespace net {

    const char *DefaultNtpHost = "pool.ntp.org";

    void sync_time_cb(struct timeval *tv) {
      Sntp::instance().synced(tv);
    }

    Sntp::Sntp() {
      _interval = sntp_get_sync_interval() / 1000.0;
      sntp_set_time_sync_notification_cb(sync_time_cb);
    }

    void Sntp::handleEvent(Event &ev) {
      if (IpEvent::is(ev, IP_EVENT_STA_GOT_IP, nullptr))
        xTimerPendFunctionCall(
            [](void *self, uint32_t a) { ((Sntp *)self)->update(); }, this, 0,
            pdMS_TO_TICKS(1000));
    }

    bool Sntp::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("sync")) {
        sntp_restart();
        req.respond();
        return true;
      }
      return false;
    }

    void time2str(char *buf, size_t bufsize) {
      time_t now;
      time(&now);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      strftime(buf, bufsize, "%F %T", &timeinfo);
    }

    void Sntp::synced(struct timeval *tv) {
      _syncedAt = millis();
      char buf[32];
      time2str(buf, sizeof(buf));
      logD("local time/date was set to %s, (TZ=%s)", buf, _tze.c_str());
      EventStateChanged::publish(this);
    }

    int Sntp::tzOfs() {
      time_t gmt, rawtime;
      time(&rawtime);
      struct tm ptm;
      gmtime_r(&rawtime, &ptm);
      ptm.tm_isdst = -1;
      gmt = mktime(&ptm);
      return (int)(difftime(rawtime, gmt) / 60);
    }

    JsonDocument *Sntp::getState(RequestContext &ctx) {
      char buf[32];
      time2str(buf, sizeof(buf));
      // size_t size = JSON_OBJECT_SIZE(3) + JSON_STRING_SIZE(strlen(buf));
      auto doc = new JsonDocument(); /* size */
      auto root = doc->to<JsonObject>();
      json::to(root, "status", (int)sntp_get_sync_status());
      if (_syncedAt)
        json::to(root, "synced", millis() - _syncedAt);
      json::to(root, "time", buf);
      return doc;
    }

    JsonDocument *Sntp::getConfig(RequestContext &ctx) {
      /*size_t size =
          JSON_OBJECT_SIZE(1 + 5 ) + // enabled, host, tzr, tze, interval
          JSON_STRING_SIZE(_host.size()) + JSON_STRING_SIZE(_tzr.size()) +
          JSON_STRING_SIZE(_tze.size());*/
      auto doc = new JsonDocument(); /* size */
      auto root = doc->to<JsonObject>();
      json::to(root, "enabled", _enabled);
      if (_host.size())
        json::to(root, "host", _host);
      else
        json::to(root, "host", DefaultNtpHost);
      json::to(root, "tzr", _tzr);
      json::to(root, "tze", _tze);
      json::to(root, "interval", _interval);
      return doc;
    }

    bool Sntp::setConfig(RequestContext &ctx) {
      JsonObjectConst obj = ctx.data.as<JsonObjectConst>();
      bool changed = false;
      json::from(obj["enabled"], _enabled, &changed);
      json::from(obj["host"], _host, &changed);
      if (!_host.size()) {
        _host = DefaultNtpHost;
        changed = true;
      }
      json::from(obj["tzr"], _tzr, &changed);
      json::from(obj["tze"], _tze, &changed);
      json::from(obj["interval"], _interval, &changed);
      if (changed)
        update();
      return changed;
    }

    std::string Sntp::host() {
      if (!_host.size())
        return DefaultNtpHost;
      else
        return _host;
    }

    void Sntp::update() {
      if (!net::isNetifInited())
        return;
      esp_sntp_stop();
      if (_enabled) {
        if (_tze.empty())
          unsetenv("TZ");
        else
          setenv("TZ", _tze.c_str(), 1);
        tzset();
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0,
                               _host.size() ? _host.c_str() : DefaultNtpHost);
        sntp_set_sync_interval((uint32_t)(_interval * 1000));
        esp_sntp_init();
        sntp_restart();
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