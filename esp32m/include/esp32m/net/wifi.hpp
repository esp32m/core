#pragma once

#include <esp_wifi.h>
#include <freertos/event_groups.h>

#include "esp32m/app.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/device.hpp"
#include "esp32m/events.hpp"
#include "esp32m/net/captive_dns.hpp"
#include "esp32m/net/interfaces.hpp"
#include "esp32m/sleep.hpp"

namespace esp32m {
  namespace net {

    class Wifi;

    namespace wifi {
      class Iface : public Interface {
       protected:
        net::Wifi *_wifi = nullptr;
        virtual void init(net::Wifi *wifi, const char *key) {
          _wifi = wifi;
          Interface::init(key);
        }
        friend class Wifi;
      };

      enum class StaStatus {
        Initial,
        Connecting,
        Connected,
        ConnectionFailed,
      };

      struct StaOptions {
        uint32_t autoRestartConnectFailures;
      };

      class Sta : public Iface {
       public:
        Sta(const Sta &) = delete;
        StaStatus status() const {
          return _status;
        }
        esp_err_t enable(bool enable);
        StaOptions &options() {
          return _options;
        }
        bool isConnected();
        esp_err_t disconnect();
        esp_err_t startWPS();

       protected:
        void init(net::Wifi *wifi, const char *key) override;

       private:
        Sta();
        StaOptions _options = {};
        StaStatus _status = StaStatus::Initial;
        void setStatus(StaStatus state);
        void getInfo(JsonObject);
        friend class net::Wifi;
      };

      enum class ApStatus {
        Initial,
        Starting,
        Running,
        Stopped,
      };

      struct ApOptions {
        uint32_t autoRestartSeconds;
      };

      class Ap : public Iface {
       public:
        Ap(const Ap &) = delete;
        esp_err_t enable(bool enable);
        bool isRunning();
        int clientsCount();
        ApStatus status() const {
          return _status;
        }

        ApOptions &options() {
          return _options;
        }

       protected:
        void init(net::Wifi *wifi, const char *key) override;

       private:
        Ap();
        ApStatus _status = ApStatus::Initial;
        CaptiveDns *_captivePortal = nullptr;
        unsigned long _apTimer = 0;
        ApOptions _options = {};
        void setStatus(ApStatus status);
        void getInfo(JsonObject);
        void stateMachine();
        friend class net::Wifi;
      };
    }  // namespace wifi

    using namespace wifi;

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
      static bool is(Event &ev, wifi_event_t event, WifiEvent **r = nullptr);

      static void publish(wifi_event_t event, void *data);

     protected:
      constexpr static const char *Type = "wifi";

     private:
      wifi_event_t _event;
      void *_data;
      WifiEvent(wifi_event_t event, void *data)
          : Event(Type), _event(event), _data(data) {}
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
      Sta &sta() {
        return _sta;
      }
      Ap *ap() {
        return _ap.get();
      }
      void stop();

      int8_t getTxp(int8_t txp) const {
        return _txp;
      }
      esp_err_t setTxp(int8_t txp) {
        if (isInitialized())
          ESP_CHECK_RETURN(esp_wifi_set_max_tx_power(txp));
        _txp = txp;
        return ESP_OK;
      }

      static const uint8_t DiagId = 10;
      static const uint8_t DiagConnected = 1;
      static const uint8_t DiagConnecting = 2;
      static const uint8_t DiagApStarting = 3;
      static const uint8_t DiagApRunning = 4;

     protected:
      JsonDocument *getState(RequestContext &ctx) override;
      bool setConfig(RequestContext &ctx) override;
      JsonDocument *getConfig(RequestContext &ctx) override;
      bool pollSensors() override;
      bool handleRequest(Request &req) override;
      void handleEvent(Event &ev) override;

     private:
      std::unique_ptr<Ap> _ap;
      Sta _sta;
      bool _stopped = false;
      TaskHandle_t _task = nullptr;
      EventGroupHandle_t _eventGroup = nullptr;
      bool _managedExternally = false;
      std::vector<ApInfo *> _aps;
      wifi_err_reason_t _errReason = (wifi_err_reason_t)0;
      int8_t _txp = 0;
      uint8_t _channel = 0;

      unsigned long _staTimer = 0, _scanStarted = 0;
      int _connectFailures = 0;
      int _maxAps = 5;
      bool _hostnameChanged = true;
      wifi_scan_config_t _scanConfig = {};

      Response *_pendingResponse = nullptr;
      std::unique_ptr<ApInfo> _connect;
      dev::Sensor _rssi;

      std::unique_ptr<JsonDocument> _delayedConfig;

      Wifi();
      esp_err_t init();
      esp_err_t mode(wifi_mode_t prev, wifi_mode_t next);
      void checkScan();
      esp_err_t checkNameChanged();
      void run();
      bool tryConnect(ApInfo *ap, bool noBssid);
      bool tryConnect();
      ApInfo *addOrUpdateAp(ApInfo *ap);
      ApInfo *addOrUpdateAp(JsonArrayConst source, bool &changed);
      void ensureId(ApInfo *ap);
      char *btmNeighborList(uint8_t *report, size_t report_len);
      friend void neighbor_report_recv_cb(void *ctx, const uint8_t *report,
                                          size_t report_len);
      friend class wifi::Ap;
      friend class wifi::Sta;
    };

    Wifi *useWifi();

    namespace wifi {
      void addAccessPoint(const char *ssid, const char *password,
                          const uint8_t *bssid = nullptr);
    }
  }  // namespace net
}  // namespace esp32m