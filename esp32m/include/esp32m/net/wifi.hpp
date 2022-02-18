#pragma once

#include <esp_wifi.h>
#include <freertos/event_groups.h>

#include "esp32m/app.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/device.hpp"
#include "esp32m/events.hpp"
#include "esp32m/sleep.hpp"
#include "esp32m/net/captive_dns.hpp"

namespace esp32m {
  namespace net {

    class WifiEvent : public Event {
     public:
      wifi_event_t event() const {
        return _event;
      }
      void *data() const {
        return _data;
      }

      bool is(wifi_event_t event) const;

      static bool is(Event &ev, WifiEvent **r);
      static bool is(Event &ev, wifi_event_t event, WifiEvent **r);

      static void publish(wifi_event_t event, void *data);

     protected:
      static const char *NAME;

     private:
      wifi_event_t _event;
      void *_data;
      WifiEvent(wifi_event_t event, void *data)
          : Event(NAME), _event(event), _data(data) {}
      friend class Wifi;
    };

    class ApInfo {
     public:
      enum Flags {
        None = 0,
        Fallback = 1,
      };
      ApInfo(uint32_t id, const char *ssid, const char *password,
             const uint8_t *bssid, Flags flags = Flags::None,
             uint8_t failcount = 0);
      ~ApInfo();
      uint32_t id() const {
        return _id;
      }
      const char *ssid() const {
        return (const char *)_buffer;
      }
      const char *password() const {
        return (const char *)_buffer + _passwordOfs;
      }
      const uint8_t *bssid() const {
        return _bssidOfs ? (const uint8_t *)_buffer + _bssidOfs : nullptr;
      }
      Flags flags() const {
        return _flags;
      }
      bool isFallback() const {
        return _flags & Flags::Fallback;
      }
      uint8_t failcount() const {
        return _failcount;
      }
      bool is(uint32_t id, const char *ssid, const uint8_t *bssid);
      bool matchesPassword(const char *pass);
      bool toJson(JsonArray &target, bool maskPassword);
      size_t jsonSize(bool maskPassword);
      void failed() {
        _failcount++;
      }
      void succeeded() {
        _failcount = 0;
      }
      bool equals(ApInfo *other);
      ApInfo *clone() {
        return new ApInfo(_id, ssid(), password(), bssid(), _flags, _failcount);
      }

     private:
      char *_buffer;
      uint32_t _id;
      uint16_t _passwordOfs, _bssidOfs;
      uint8_t _failcount;
      Flags _flags;
      friend class Wifi;
    };

    ENUM_FLAG_OPERATORS(ApInfo::Flags)

    class Wifi : public Device {
     public:
      Wifi(const Wifi &) = delete;
      enum class StaState {
        Initial,
        Connecting,
        Connected,
        ConnectionFailed,
      };

      enum class ApState {
        Initial,
        Starting,
        Running,
        Stopped,
      };

      static Wifi &instance();
      const char *name() const override {
        return "wifi";
      }
      ApInfo *addFallback(const char *ssid, const char *password,
                          const uint8_t *bssid = nullptr) {
        return addOrUpdateAp(
            new ApInfo(0, ssid, password, bssid, ApInfo::Flags::Fallback, 0));
      }
      bool isInitialized() const;
      bool isConnected() const;
      int apClientsCount();
      esp_err_t sta(bool enable);
      esp_err_t ap(bool enable);
      esp_err_t disconnect();
      void stop();
      const esp_netif_ip_info_t &apIp() const {
        return _apIp;
      }
      const esp_netif_ip_info_t &staIp() const {
        return _staIp;
      }

     protected:
      // void setState(const JsonVariantConst cfg) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;
      bool pollSensors() override;
      bool handleRequest(Request &req) override;
      bool handleEvent(Event &ev) override;

     private:
      Wifi();
      bool _stopped=false;
      esp_netif_t *_ifsta = nullptr, *_ifap = nullptr;
      TaskHandle_t _task = nullptr;
      EventGroupHandle_t _eventGroup = nullptr;
      std::vector<ApInfo *> _aps;
      bool _staDhcp = true;
      wifi_err_reason_t _errReason = (wifi_err_reason_t)0;
      esp_netif_ip_info_t _staIp = {}, _apIp;
      ip_addr_t _dns1 = {}, _dns2 = {};
      bool _useNtp = true;
      char *_ntpHost;
      int _dstOfs = 0, _tzOfs = 0;
      int8_t _txp = 0;

      unsigned long _staTimer = 0, _apTimer = 0, _scanStarted = 0;
      int _connectFailures = 0;
      int _maxAps = 5;
      wifi_scan_config_t _scanConfig = {.ssid = nullptr,
                                        .bssid = nullptr,
                                        .channel = 0,
                                        .show_hidden = true,
                                        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                                        .scan_time = {}};

      StaState _staState = StaState::Initial;
      ApState _apState = ApState::Initial;
      Response *_pendingResponse = nullptr;
      CaptiveDns *_captivePortal = nullptr;
      std::unique_ptr<ApInfo> _connect;
      void init();
      esp_err_t mode(wifi_mode_t prev, wifi_mode_t next);
      void setState(StaState state);
      void setState(ApState state);
      void updateTimeConfig();
      void checkScan();
      void run();
      bool tryConnect(ApInfo *ap, bool noBssid);
      bool tryConnect();
      ApInfo *addOrUpdateAp(ApInfo *ap);
      ApInfo *addOrUpdateAp(JsonArrayConst source, bool &changed);
      void ensureId(ApInfo *ap);
      void staInfo(JsonObject);
      void apInfo(JsonObject);
      char *btmNeighborList(uint8_t *report, size_t report_len);
      friend void neighbor_report_recv_cb(void *ctx, const uint8_t *report, size_t report_len);
    };

    Wifi *useWifi();

    namespace wifi {
      void addAccessPoint(const char *ssid, const char *password,
                          const uint8_t *bssid = nullptr);
    }
  }  // namespace net
}  // namespace esp32m