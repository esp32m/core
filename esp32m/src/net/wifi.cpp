#include "esp32m/net/wifi.hpp"
#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/events/diag.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/integrations/ha/ha.hpp"
#include "esp32m/net/ip_event.hpp"
#include "esp32m/net/net.hpp"
#include "esp32m/props.hpp"

// #include <dhcpserver/dhcpserver.h>
// #include <dhcpserver/dhcpserver_options.h>
#include <esp_mac.h>
#include <esp_mbo.h>
#include <esp_netif.h>
#include <esp_netif_types.h>
#include <esp_rrm.h>
#include <esp_task_wdt.h>
#include <esp_wnm.h>
#include <lwip/dns.h>
#include <algorithm>

#include <sdkconfig.h>

namespace esp32m {
  namespace net {

    static bool sta_config_equal(const wifi_config_t &lhs,
                                 const wifi_config_t &rhs) {
      return memcmp(&lhs, &rhs, sizeof(wifi_config_t)) == 0;
    }

    enum WifiFlags {
      Initialized = BIT0,
      StaRunning = BIT1,
      StaConnected = BIT2,
      StaGotIp = BIT3,
      ApRunning = BIT4,
      Scanning = BIT5,
      ScanDone = BIT6,
    };

    namespace wifi {

      Sta::Sta() {
        _role = Role::DhcpClient;
      };

      esp_err_t Sta::enable(bool enable) {
        wifi_mode_t m = WIFI_MODE_NULL;
        ESP_CHECK_RETURN(esp_wifi_get_mode(&m));
        return _wifi->mode(m, enable ? (wifi_mode_t)(m | WIFI_MODE_STA)
                                     : (wifi_mode_t)(m & ~WIFI_MODE_STA));
      }

      bool Sta::isConnected() {
        return xEventGroupGetBits(_wifi->_eventGroup) & WifiFlags::StaConnected;
      }

      esp_err_t Sta::disconnect() {
        if (isConnected())
          return ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
        return ESP_OK;
      }

      void Sta::getInfo(JsonObject info) {
        if (isConnected()) {
          auto infoSta = info.createNestedObject("sta");
          wifi_ap_record_t info;
          if (!esp_wifi_sta_get_ap_info(&info)) {
            // char sbssid[18] = {0};
            infoSta["ssid"] = info.ssid;
            json::macTo(infoSta, "bssid", info.bssid);
            /*if (formatBssid(info.bssid, sbssid))
              infoSta["bssid"] = sbssid;*/
            uint8_t mac[6];
            if (!esp_wifi_get_mac(WIFI_IF_STA,
                                  mac) /*&& formatBssid(mac, sbssid)*/)
              json::macTo(infoSta, mac);
            // infoSta["mac"] = sbssid;
            infoSta["rssi"] = info.rssi;
          }
          esp_netif_ip_info_t ip;
          if (!ESP_ERROR_CHECK_WITHOUT_ABORT(getIpInfo(ip)))
            json::to(infoSta, ip);
        }
      }

      const char *StaStatusNames[]{
          "initial",
          "connecting",
          "connected",
          "connection-failed",
      };

      void Sta::setStatus(StaStatus state) {
        if (state == _status)
          return;
        logI("%s -> %s", StaStatusNames[(int)_status],
             StaStatusNames[(int)state]);
        int diagCode = -1;
        switch (state) {
          case StaStatus::Connecting:
            diagCode = Wifi::DiagConnecting;
            break;
          case StaStatus::Connected:
            diagCode = Wifi::DiagConnected;
            break;
          default:
            break;
        }
        if (diagCode > 0)
          event::Diag::publish(Wifi::DiagId, diagCode);
        _status = state;
      }

      const esp_ip4_addr_t DefaultNetmask = {
          PP_HTONL(LWIP_MAKEU32(255, 255, 255, 0))};
      const esp_ip4_addr_t DefaultApIp = {
          PP_HTONL(LWIP_MAKEU32(192, 168, 4, 1))};

      Ap::Ap() {
        _role = Role::DhcpServer;
        _ip = {DefaultApIp, DefaultNetmask, DefaultApIp};
        _dhcpsLease.enable = true;
        _dhcpsLease.start_ip.addr =
            static_cast<uint32_t>(_ip.ip.addr) + (1 << 24);
        _dhcpsLease.end_ip.addr =
            static_cast<uint32_t>(_ip.ip.addr) + (11 << 24);
        /*        esp_netif_dns_info_t dns;
                dns.ip.u_addr.ip4.addr = _ip.ip.addr;
                dns.ip.type = IPADDR_TYPE_V4;
                _dns[ESP_NETIF_DNS_MAIN] = dns;*/
      };

      void Ap::init(net::Wifi *wifi, const char *key) {
        auto handle = esp_netif_get_handle_from_ifkey(key);
        if (handle) {
          // we can't just set _dns because it will be overwritten on synchandle
          esp_netif_dns_info_t dns;
          dns.ip.u_addr.ip4.addr = _ip.ip.addr;
          dns.ip.type = IPADDR_TYPE_V4;
          esp_netif_set_dns_info(handle, ESP_NETIF_DNS_MAIN, &dns);
        }
        Iface::init(wifi, key);
      }

      esp_err_t Ap::enable(bool enable) {
        if (_wifi->_managedExternally)
          return ESP_OK;
        wifi_mode_t m = WIFI_MODE_NULL;
        ESP_CHECK_RETURN(esp_wifi_get_mode(&m));
        if (enable) {
          /*if (m & WIFI_MODE_AP)
            return ESP_OK;*/
          _apTimer = millis();
          // vTaskDelay(pdMS_TO_TICKS(100)); // allow some time for the
          // events to fire
          setStatus(ApStatus::Starting);
        }
        esp_err_t result =
            _wifi->mode(m, enable ? (wifi_mode_t)(m | WIFI_MODE_AP)
                                  : (wifi_mode_t)(m & ~WIFI_MODE_AP));
        if (result != ESP_OK)
          return result;
        ErrorList errl;
        stopDhcp();
        if (enable) {
          apply(errl);
          errl.dump();

          wifi_config_t current_conf;
          ESP_CHECK_RETURN(esp_wifi_get_config(WIFI_IF_AP, &current_conf));

          wifi_config_t conf;
          memset(&conf, 0, sizeof(wifi_config_t));
          strlcpy(reinterpret_cast<char *>(conf.ap.ssid),
                  App::instance().hostname(), sizeof(conf.ap.ssid));
          conf.ap.channel = 1;
          conf.ap.authmode = WIFI_AUTH_OPEN;
          conf.ap.ssid_len = strlen(reinterpret_cast<char *>(conf.ap.ssid));
          conf.ap.max_connection = 4;
          conf.ap.beacon_interval = 100;
          if (!sta_config_equal(current_conf, conf)) {
            logI("setting AP config");
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_wifi_set_config(WIFI_IF_AP, &conf));
          }
        } else if (_status != ApStatus::Initial)
          setStatus(ApStatus::Stopped);
        return ESP_OK;
      }

      void Ap::getInfo(JsonObject info) {
        auto infoAp = info.createNestedObject("ap");
        const char *hostname = NULL;
        if (!esp_netif_get_hostname(handle(), &hostname))
          infoAp["ssid"] = (char *)hostname;
        uint8_t mac[6];
        // char sbssid[18] = {0};
        if (!esp_wifi_get_mac(WIFI_IF_AP, mac) /*&& formatBssid(mac, sbssid)*/)
          json::macTo(infoAp, mac);
        // infoAp["mac"] = sbssid;
        wifi_sta_list_t clients;
        if (!esp_wifi_ap_get_sta_list(&clients))
          infoAp["cli"] = clients.num;

        esp_netif_ip_info_t ip;
        if (!ESP_ERROR_CHECK_WITHOUT_ABORT(getIpInfo(ip)))
          json::to(infoAp, ip);
      }

      const char *ApStateNames[]{"initial", "starting", "running", "stopped"};

      void Ap::setStatus(ApStatus state) {
        if (state == _status)
          return;
        logI("%s -> %s", ApStateNames[(int)_status], ApStateNames[(int)state]);
        int diagCode = -1;
        switch (state) {
          case ApStatus::Starting:
            diagCode = Wifi::DiagApStarting;
            break;
          case ApStatus::Running:
            diagCode = Wifi::DiagApRunning;
            break;
          default:
            break;
        }
        if (diagCode > 0)
          event::Diag::publish(Wifi::DiagId, diagCode);
        _status = state;
      }

      bool Ap::isRunning() {
        return xEventGroupGetBits(_wifi->_eventGroup) & WifiFlags::ApRunning;
      }

      int Ap::clientsCount() {
        if (!isRunning())
          return 0;
        wifi_sta_list_t clients;
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_ap_get_sta_list(&clients)) !=
            ESP_OK)
          return 0;
        return clients.num;
      }

      void Ap::stateMachine() {
        auto curtime = millis();
        switch (_status) {
          case ApStatus::Starting:
            if (isRunning()) {
              setStatus(ApStatus::Running);
              _apTimer = curtime;
              break;
            }
            if (curtime - _apTimer <= 5000)
              break;
            logI("could not start AP, falling back to STA");
            setStatus(ApStatus::Initial);
            break;
          case ApStatus::Running: {
            /*if (!(xEventGroupGetBits(_eventGroup) & WifiFlags::ApRunning))
            {
              // we can't rely on ApRunning status because AP_STOP/AP_START
            events may be fired multiple times during AP startup
              _ap.setStatus(ApStatus::Stopped);
              break;
            }*/
            if (clientsCount() > 0)
              _apTimer = curtime;
            if (curtime - _apTimer >= 60000) {
              if (_wifi->isConnected()) {
                logW("disabling AP because STA is connected");
                enable(false);
              }
              delay(100);  // allow some time for the events to fire
              break;
            }
            if (_options.autoRestartSeconds > 0 &&
                (curtime - _apTimer > _options.autoRestartSeconds * 1000) &&
                !_wifi->isConnected()) {
              logW("no clients connected within 60s, restarting...");
              App::restart();
            }
            if (!_captivePortal)
              _captivePortal = new CaptiveDns(_ip.ip);
            _captivePortal->enable(true);
          } break;
          case ApStatus::Stopped:
            if (_captivePortal)
              _captivePortal->enable(false);
            setStatus(ApStatus::Initial);
            break;
          default:
            break;
        };
      }

      static const char *getDefaultStaKey() {
        const esp_netif_inherent_config_t config =
            ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
        return config.if_key;
      }

#ifdef CONFIG_ESP_WIFI_SOFTAP_SUPPORT
      static const char *getDefaultApKey() {
        const esp_netif_inherent_config_t config =
            ESP_NETIF_INHERENT_DEFAULT_WIFI_AP();
        return config.if_key;
      }
#endif
    }  // namespace wifi

    const char *ModeNames[]{"Disabled", "STA", "AP", "AP+STA"};

    const char *WifiEvent::NAME = "wifi";

    bool WifiEvent::is(wifi_event_t event) const {
      return _event == event;
    }

    bool WifiEvent::is(Event &ev, WifiEvent **r) {
      if (!ev.is(NAME))
        return false;
      if (r)
        *r = (WifiEvent *)&ev;
      return true;
    }

    bool WifiEvent::is(Event &ev, wifi_event_t event, WifiEvent **r) {
      if (!ev.is(NAME))
        return false;
      wifi_event_t t = ((WifiEvent &)ev)._event;
      if (t != event)
        return false;
      if (r)
        *r = (WifiEvent *)&ev;
      return true;
    }

    void WifiEvent::publish(wifi_event_t event, void *data) {
      WifiEvent ev(event, data);
      EventManager::instance().publish(ev);
    }

    ApInfo::ApInfo(uint32_t id, const char *ssid, const char *password,
                   const uint8_t *bssid, Flags flags, uint8_t failcount) {
      _id = id;
      auto sl = ssid ? strlen(ssid) : 0;
      auto pl = password ? strlen(password) : 0;
      _buffer = (char *)malloc(sl + pl + 2 + (bssid ? 6 : 0));
      _passwordOfs = sl + 1;
      if (sl)
        memcpy(_buffer, ssid, _passwordOfs);
      else
        _buffer[0] = 0;
      if (pl)
        memcpy(_buffer + _passwordOfs, password, pl + 1);
      else
        _buffer[_passwordOfs] = 0;
      if (bssid) {
        _bssidOfs = _passwordOfs + pl + 1;
        memcpy(_buffer + _bssidOfs, bssid, 6);
      } else
        _bssidOfs = 0;
      _flags = flags;
      _failcount = failcount;
    }

    ApInfo::~ApInfo() {
      free(_buffer);
    }
    bool ApInfo::is(uint32_t id, const char *ssid, const uint8_t *bssid) {
      if (id)
        return id == _id;
      const char *mySsid = this->ssid();
      if (!ssid && strlen(mySsid))
        return false;
      if (strcmp(ssid, mySsid))
        return false;
      if (!bssid)
        return true;
      return _bssidOfs && !memcmp(bssid, this->bssid(), 6);
    }
    bool ApInfo::matchesPassword(const char *pass) {
      auto pl = pass ? strlen(pass) : 0;
      if (pl != strlen(password()))
        return false;
      if (!pl)
        return true;
      return !strcmp(pass, password());
    }

    bool ApInfo::equals(ApInfo *other) {
      if (this == other)
        return true;
      if (_id != other->_id || _flags != other->_flags ||
          _failcount != other->_failcount)
        return false;
      if (strcmp(ssid(), other->ssid()) ||
          !strcmp(password(), other->password()))
        return false;
      if (_bssidOfs != other->_bssidOfs)
        return false;
      if (!_bssidOfs)
        return true;
      return !memcmp(bssid(), other->bssid(), 6);
    }

    size_t ApInfo::jsonSize(bool maskPassword) {
      return JSON_ARRAY_SIZE(5 + (_bssidOfs ? 1 : 0)) + strlen(ssid()) + 1 +
             (maskPassword ? 0 : (strlen(password()) + 1)) +
             (_bssidOfs ? 19 : 0);
    }

    bool ApInfo::toJson(JsonArray &target, bool maskPassword) {
      auto result = target.add(_id) && target.add((char *)ssid()) &&
                    (maskPassword ? target.add(nullptr)
                                  : target.add((char *)password())) &&
                    target.add((int)_flags) && target.add(_failcount);
      if (!result)
        return false;
      if (_bssidOfs)
        result = json::macTo(target, bssid());

      /*char bssidStr[18];
      if (formatBssid(bssid(), bssidStr))
        result = target.add(bssidStr);*/
      return result;
    }

    Wifi::Wifi() : _rssi(this, "signal_strength", "rssi") {
      Device::init(Flags::HasSensors);
      _eventGroup = xEventGroupCreate();
    }

    bool Wifi::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("scan")) {
        xEventGroupClearBits(_eventGroup, WifiFlags::ScanDone);
        _pendingResponse = req.makeResponse();
        xTaskNotifyGive(_task);
        return true;
      }
      if (req.is("connect")) {
        auto data = req.data();
        uint8_t bssid[6];
        _connect = std::unique_ptr<ApInfo>(new ApInfo(
            0, data["ssid"].as<const char *>(),
            data["password"].as<const char *>(),
            net::macParse(data["bssid"].as<const char *>(), bssid) ? bssid
                                                                   : nullptr));
        req.respond();
        _sta.disconnect();
        xTaskNotifyGive(_task);
        return true;
      }
      if (req.is("delete-ap")) {
        uint32_t id = req.data()["id"];
        esp_err_t error = ESP_OK;
        if (!id)
          error = ESP_ERR_INVALID_ARG;
        else
          for (auto it = _aps.begin(); it != _aps.end(); ++it)
            if ((*it)->id() == id) {
              if (!(*it)->isFallback())
                it = _aps.erase(it);
              else
                error = ESP_ERR_INVALID_ARG;
              break;
            }
        if (!error)
          config::Changed::publish(this);
        req.respond(error);
        return true;
      }
      if (req.is(integrations::ha::DescribeRequest::Name)) {
        integrations::ha::DescribeRequest::autoRespond(req, _rssi);
        return true;
      }
      return false;
    }

    void wifi_event_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data) {
      WifiEvent::publish((wifi_event_t)event_id, event_data);
    }

    void ip_event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
      IpEvent::publish((ip_event_t)event_id, event_data);
    }

    static inline uint32_t WPA_GET_LE32(const uint8_t *a) {
      return ((uint32_t)a[3] << 24) | (a[2] << 16) | (a[1] << 8) | a[0];
    }
#ifndef WLAN_EID_MEASURE_REPORT
#  define WLAN_EID_MEASURE_REPORT 39
#endif
#ifndef MEASURE_TYPE_LCI
#  define MEASURE_TYPE_LCI 9
#endif
#ifndef MEASURE_TYPE_LOCATION_CIVIC
#  define MEASURE_TYPE_LOCATION_CIVIC 11
#endif
#ifndef WLAN_EID_NEIGHBOR_REPORT
#  define WLAN_EID_NEIGHBOR_REPORT 52
#endif
#ifndef ETH_ALEN
#  define ETH_ALEN 6
#endif
#define MAX_NEIGHBOR_LEN 512
    char *Wifi::btmNeighborList(uint8_t *report, size_t report_len) {
      size_t len = 0;
      const uint8_t *data;
      int ret = 0;

      /*
       * Neighbor Report element (IEEE P802.11-REVmc/D5.0)
       * BSSID[6]
       * BSSID Information[4]
       * Operating Class[1]
       * Channel Number[1]
       * PHY Type[1]
       * Optional Subelements[variable]
       */
#define NR_IE_MIN_LEN (ETH_ALEN + 4 + 1 + 1 + 1)

      if (!report || report_len == 0) {
        logI("RRM neighbor report is not valid");
        return nullptr;
      }

      char *buf = (char *)calloc(1, MAX_NEIGHBOR_LEN);
      data = report;

      while (report_len >= 2 + NR_IE_MIN_LEN) {
        const uint8_t *nr;
        char lci[256 * 2 + 1];
        char civic[256 * 2 + 1];
        uint8_t nr_len = data[1];
        const uint8_t *pos = data, *end;

        if (pos[0] != WLAN_EID_NEIGHBOR_REPORT || nr_len < NR_IE_MIN_LEN) {
          logI("CTRL: Invalid Neighbor Report element: id=%u len=%u", data[0],
               nr_len);
          ret = -1;
          goto cleanup;
        }

        if (2U + nr_len > report_len) {
          logI("CTRL: Invalid Neighbor Report element: id=%u len=%zu nr_len=%u",
               data[0], report_len, nr_len);
          ret = -1;
          goto cleanup;
        }
        pos += 2;
        end = pos + nr_len;

        nr = pos;
        pos += NR_IE_MIN_LEN;

        lci[0] = '\0';
        civic[0] = '\0';
        while (end - pos > 2) {
          uint8_t s_id, s_len;

          s_id = *pos++;
          s_len = *pos++;
          if (s_len > end - pos) {
            ret = -1;
            goto cleanup;
          }
          if (s_id == WLAN_EID_MEASURE_REPORT && s_len > 3) {
            /* Measurement Token[1] */
            /* Measurement Report Mode[1] */
            /* Measurement Type[1] */
            /* Measurement Report[variable] */
            switch (pos[2]) {
              case MEASURE_TYPE_LCI:
                if (lci[0])
                  break;
                memcpy(lci, pos, s_len);
                break;
              case MEASURE_TYPE_LOCATION_CIVIC:
                if (civic[0])
                  break;
                memcpy(civic, pos, s_len);
                break;
            }
          }

          pos += s_len;
        }

        logI("RMM neigbor report bssid=" MACSTR
             " info=0x%x op_class=%u chan=%u phy_type=%u%s%s%s%s",
             MAC2STR(nr), WPA_GET_LE32(nr + ETH_ALEN), nr[ETH_ALEN + 4],
             nr[ETH_ALEN + 5], nr[ETH_ALEN + 6], lci[0] ? " lci=" : "", lci,
             civic[0] ? " civic=" : "", civic);

        /* neighbor start */
        len += snprintf(buf + len, MAX_NEIGHBOR_LEN - len, " neighbor=");
        /* bssid */
        len += snprintf(buf + len, MAX_NEIGHBOR_LEN - len, MACSTR, MAC2STR(nr));
        /* , */
        len += snprintf(buf + len, MAX_NEIGHBOR_LEN - len, ",");
        /* bssid info */
        len += snprintf(buf + len, MAX_NEIGHBOR_LEN - len, "0x%04x",
                        (unsigned int)WPA_GET_LE32(nr + ETH_ALEN));
        len += snprintf(buf + len, MAX_NEIGHBOR_LEN - len, ",");
        /* operating class */
        len +=
            snprintf(buf + len, MAX_NEIGHBOR_LEN - len, "%u", nr[ETH_ALEN + 4]);
        len += snprintf(buf + len, MAX_NEIGHBOR_LEN - len, ",");
        /* channel number */
        len +=
            snprintf(buf + len, MAX_NEIGHBOR_LEN - len, "%u", nr[ETH_ALEN + 5]);
        len += snprintf(buf + len, MAX_NEIGHBOR_LEN - len, ",");
        /* phy type */
        len +=
            snprintf(buf + len, MAX_NEIGHBOR_LEN - len, "%u", nr[ETH_ALEN + 6]);
        /* optional elements, skip */

        data = end;
        report_len -= 2 + nr_len;
      }

    cleanup:
      if (ret < 0) {
        free(buf);
        buf = nullptr;
      }
      return buf;
    }

    void neighbor_report_recv_cb(void *ctx, const uint8_t *report,
                                 size_t report_len) {
      if (ctx != &Wifi::instance() || !report)
        return;
      uint8_t *pos = (uint8_t *)report;
      char *neighbor_list =
          ((Wifi *)ctx)->btmNeighborList(pos + 1, report_len - 1);
      if (neighbor_list) {
        esp_wnm_send_bss_transition_mgmt_query(REASON_FRAME_LOSS, neighbor_list,
                                               0);
        free(neighbor_list);
      }
    }

    esp_err_t Wifi::init() {
      static bool inited = false;
      if (inited)
        return ESP_OK;
      inited = true;

      esp_netif_t *ifsta = nullptr, *ifap = nullptr;

      ifsta = esp_netif_get_handle_from_ifkey(getDefaultStaKey());
#ifdef CONFIG_ESP_WIFI_SOFTAP_SUPPORT
      ifap = esp_netif_get_handle_from_ifkey(getDefaultApKey());
#endif
      // no need to fire net::IfEventType::Created because we subclass
      // net::IFace and call init()
      if (!ifsta)
        ifsta = esp_netif_create_default_wifi_sta();
#ifdef CONFIG_ESP_WIFI_SOFTAP_SUPPORT
      if (!ifap)
        ifap = esp_netif_create_default_wifi_ap();
#endif
      if (!_managedExternally) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_CHECK_RETURN(esp_wifi_init(&cfg));
        ESP_CHECK_RETURN(esp_wifi_set_storage(WIFI_STORAGE_RAM));
      }
      ESP_CHECK_RETURN(esp_event_handler_instance_register(
          WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr, nullptr));
      ESP_CHECK_RETURN(esp_event_handler_instance_register(
          IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, nullptr, nullptr));
      xEventGroupSetBits(_eventGroup, WifiFlags::Initialized);
      if (!_managedExternally)
        ESP_CHECK_RETURN(esp_wifi_start());
      _errReason = (wifi_err_reason_t)0;
      /*
      WifiEvent::publish(WIFI_EVENT_WIFI_READY,
                         nullptr);  // this event is never sent by esp-idf, so
                                    // we do it in case someone wants it
      */

      _sta.init(this, esp_netif_get_ifkey(ifsta));

#ifdef CONFIG_ESP_WIFI_SOFTAP_SUPPORT
      _ap.reset(new Ap());
      _ap->init(this, esp_netif_get_ifkey(ifap));
      _ap->enable(false);
#endif

      if (_txp)
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_max_tx_power(_txp));
      else
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_get_max_tx_power(&_txp));
      if (_task)
        xTaskNotifyGive(_task);
      return ESP_OK;
    }

    esp_err_t Wifi::checkNameChanged() {
      if (!(_hostnameChanged && isInitialized()))
        return ESP_OK;
      _hostnameChanged = false;
      ErrorList errl;
      if (_ap)
        _ap->apply(Interface::ConfigItem::Hostname, errl);
      _sta.apply(Interface::ConfigItem::Hostname, errl);
      return ESP_OK;
    }

    bool Wifi::isInitialized() const {
      return xEventGroupGetBits(_eventGroup) & WifiFlags::Initialized;
    }

    bool Wifi::isConnected() const {
      return (xEventGroupGetBits(_eventGroup) &
              (WifiFlags::StaConnected | WifiFlags::StaGotIp)) ==
             (WifiFlags::StaConnected | WifiFlags::StaGotIp);
    }

    void Wifi::handleEvent(Event &ev) {
      Device::handleEvent(ev);
      IpEvent *ip;
      WifiEvent *wifi;
      sleep::Event *slev;
      DoneReason reason;
      bool wakeup = false;
      if (EventInit::is(ev, 0)) {
        // useNetif() must be called on the main thread, because other services
        // may use tcpip during init
        esp_netif_t *ifsta = nullptr, *ifap = nullptr;

        ifsta = esp_netif_get_handle_from_ifkey(getDefaultStaKey());
#ifdef CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        ifap = esp_netif_get_handle_from_ifkey(getDefaultApKey());
#endif
        if (ifsta || ifap)
          _managedExternally = true;
        if (!_managedExternally) {
          ESP_ERROR_CHECK_WITHOUT_ABORT(useNetif());
          ESP_ERROR_CHECK_WITHOUT_ABORT(useEventLoop());
        }

        xTaskCreate([](void *self) { ((Wifi *)self)->run(); }, "m/wifi", 5120,
                    this, tskIDLE_PRIORITY + 1, &_task);
      } else if (EventDone::is(ev, &reason)) {
        stop();
      } else if (EventPropChanged::is(ev, "app", "hostname")) {
        _hostnameChanged = true;
        if (_task)
          xTaskNotifyGive(_task);
      } else if (WifiEvent::is(ev, &wifi)) {
        wakeup = true;
        switch (wifi->event()) {
          case WIFI_EVENT_STA_START:
            xEventGroupSetBits(_eventGroup, WifiFlags::StaRunning);
            break;
          case WIFI_EVENT_STA_STOP:
            xEventGroupClearBits(_eventGroup, WifiFlags::StaRunning);
            break;
          case WIFI_EVENT_STA_CONNECTED:
            // esp_wifi_set_rssi_threshold(-67);
            xEventGroupSetBits(_eventGroup, WifiFlags::StaConnected);
            break;
          case WIFI_EVENT_STA_DISCONNECTED: {
            auto r = (wifi_event_sta_disconnected_t *)wifi->data();
            _errReason = (wifi_err_reason_t)r->reason;
            xEventGroupClearBits(_eventGroup,
                                 WifiFlags::StaConnected | WifiFlags::StaGotIp);
            break;
          }
          case WIFI_EVENT_STA_BSS_RSSI_LOW: {
            int e = esp_rrm_send_neighbor_rep_request(neighbor_report_recv_cb,
                                                      this);
            if (e < 0) {
              /* failed to send neighbor report request */
              logW("failed to send neighbor report request: %i", e);
              e = esp_wnm_send_bss_transition_mgmt_query(REASON_FRAME_LOSS,
                                                         NULL, 0);
              if (e < 0) {
                logI("failed to send btm query: %i", e);
              }
            }
          } break;
          case WIFI_EVENT_SCAN_DONE:
            // logI("scan done");
            xEventGroupClearBits(_eventGroup, WifiFlags::Scanning);
            xEventGroupSetBits(_eventGroup, WifiFlags::ScanDone);
            break;
          case WIFI_EVENT_AP_START:
            // logI("AP started");
            // for some reason, AP_STOP/AP_START events are fired multiple
            // times
            xEventGroupSetBits(_eventGroup, WifiFlags::ApRunning);
            break;
          case WIFI_EVENT_AP_STOP:
            // logI("AP stopped");
            xEventGroupClearBits(_eventGroup, WifiFlags::ApRunning);
            break;
          /*case WIFI_EVENT_AP_STACONNECTED:
  {
  auto r = (wifi_event_ap_staconnected_t *)wifi->data();
  char mac[18];
  formatBssid(r->mac, mac);
  logI("STA %s connected, id=%d", mac, r->aid);
  break;
  }
  case WIFI_EVENT_AP_STADISCONNECTED:
  {
  auto r = (wifi_event_ap_stadisconnected_t *)wifi->data();
  char mac[18];
  formatBssid(r->mac, mac);
  logI("STA %s disconnected, id=%d", mac, r->aid);
  break;
  }*/
          default:
            wakeup = false;
            break;
        }
      } else if (IpEvent::is(ev, &ip)) {
        wakeup = true;
        switch (ip->event()) {
          case IP_EVENT_STA_GOT_IP: {
            _errReason = (wifi_err_reason_t)0;
            ip_event_got_ip_t *d = (ip_event_got_ip_t *)ip->data();
            const esp_netif_ip_info_t *ip_info = &d->ip_info;
            logI("my IP: " IPSTR, IP2STR(&ip_info->ip));
            xEventGroupSetBits(_eventGroup, WifiFlags::StaGotIp);
            break;
          }
          case IP_EVENT_STA_LOST_IP:
            xEventGroupClearBits(_eventGroup, WifiFlags::StaGotIp);
            break;
          default:
            wakeup = false;
            break;
        }
      } else if (sleep::Event::is(ev, &slev) && !isConnected())
        slev->block();

      if (_task && wakeup)
        xTaskNotifyGive(_task);
    }

    DynamicJsonDocument *Wifi::getState(const JsonVariantConst filter) {
      size_t size =
          JSON_OBJECT_SIZE(4 + 3)  // mode, flags, ch, ch2  + nested: sta, ap
          + JSON_OBJECT_SIZE(8) + 33 + net::MacMaxChars * 2 + 16 + 16 +
          16  // sta: ssid(33), bssid(18), mac(18), ip(16),
              // gw(16), mask(16), rssi
          + JSON_OBJECT_SIZE(6) + net::MacMaxChars + 16 + 16 +
          16;  // ap: mac(18), ip(16), gw(16), mask(16), cli
      auto doc = new DynamicJsonDocument(size);
      auto info = doc->to<JsonObject>();
      wifi_mode_t wfm = WIFI_MODE_NULL;
      if (esp_wifi_get_mode(&wfm) == ESP_OK) {
        info["mode"] = (int)wfm;
        info["flags"] = xEventGroupGetBits(_eventGroup);
        uint8_t pc = 0;
        wifi_second_chan_t sc = WIFI_SECOND_CHAN_NONE;
        if (esp_wifi_get_channel(&pc, &sc) == ESP_OK) {
          if (pc)
            info["ch"] = pc;
          if (sc)
            info["ch2"] = sc;
        }
        switch (wfm) {
          case WIFI_MODE_STA:
            _sta.getInfo(info);
            break;
          case WIFI_MODE_AP:
            if (_ap)
              _ap->getInfo(info);
            break;
          case WIFI_MODE_APSTA:
            if (_ap)
              _ap->getInfo(info);
            _sta.getInfo(info);
            break;
          default:
            break;
        }
      }
      return doc;
    }

    void getIfcfg(wifi_interface_t ifx, JsonObject target) {
      uint8_t proto;
      if (!ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_get_protocol(ifx, &proto)))
        target["proto"] = proto;
      wifi_bandwidth_t bw;
      if (!ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_get_bandwidth(ifx, &bw)))
        target["bw"] = bw;
      uint16_t inact;
      if (!esp_wifi_get_inactive_time(ifx, &inact))
        target["inact"] = inact;
    }

    void setIfcfg(wifi_interface_t ifx, JsonObjectConst source) {}

    DynamicJsonDocument *Wifi::getConfig(RequestContext &ctx) {
      size_t size = JSON_OBJECT_SIZE(2) +      // txp, channel
                    JSON_OBJECT_SIZE(1 + 3) +  // sta: proto, bw, inact
                    JSON_OBJECT_SIZE(1 + 3);   // ap:  proto, bw, inact,

      bool maskSensitive = !ctx.request.isInternal();
      size_t apsCount = _aps.size();
      if (apsCount) {
        size += JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(apsCount);
        for (auto it = _aps.begin(); it != _aps.end(); it++)
          size += (*it)->jsonSize(maskSensitive);
      }

      auto doc = new DynamicJsonDocument(size);
      auto cr = doc->to<JsonObject>();
      cr["txp"] = _txp;
      cr["channel"] = _channel;
      auto sta = cr.createNestedObject("sta");
      getIfcfg(WIFI_IF_STA, sta);
      auto ap = cr.createNestedObject("ap");
      getIfcfg(WIFI_IF_AP, ap);
      if (apsCount) {
        auto aps = cr.createNestedArray("aps");
        for (auto it = _aps.begin(); it != _aps.end(); it++) {
          JsonArray item = aps.createNestedArray();
          (*it)->toJson(item, maskSensitive);
        }
      }
      return doc;
    }

    bool Wifi::setConfig(const JsonVariantConst data,
                         DynamicJsonDocument **result) {
      JsonObjectConst ca = data.as<JsonObjectConst>();
      int8_t txp = ca["txp"];
      bool changed = false;
      esp_err_t err = ESP_OK;
      if (txp != _txp)
        if (!isInitialized() || !txp ||
            (err = ESP_ERROR_CHECK_WITHOUT_ABORT(
                 esp_wifi_set_max_tx_power(txp))) == ESP_OK) {
          _txp = txp;
          changed = true;
        }
      json::from(ca["channel"], _channel, &changed);
      JsonObjectConst obj = ca["sta"];
      if (obj)
        setIfcfg(WIFI_IF_STA, obj);

      obj = ca["ap"];
      if (obj)
        setIfcfg(WIFI_IF_AP, obj);

      JsonArrayConst aps = ca["aps"];
      if (aps)
        for (JsonArrayConst v : aps) addOrUpdateAp(v, changed);
      json::checkSetResult(err, result);
      return changed;
    }

    bool Wifi::tryConnect(ApInfo *ap, bool noBssid) {
      esp_task_wdt_reset();
      _sta.disconnect();
      if (_sta.enable(true) != ESP_OK)
        return false;
      if (ap) {
        wifi_config_t conf;
        memset(&conf, 0, sizeof(wifi_config_t));
        const char *ssid = ap->ssid();
        strlcpy(reinterpret_cast<char *>(conf.sta.ssid), ssid,
                sizeof(conf.sta.ssid));
        const uint8_t *bssid = ap->bssid();
        char bssidStr[net::MacMaxChars] = "any";
        if (_channel)
          conf.sta.channel = _channel;
        if (bssid && !noBssid) {
          conf.sta.bssid_set = 1;
          memcpy((void *)&conf.sta.bssid[0], bssid, 6);
          sprintf(bssidStr, MACSTR, MAC2STR(bssid));
        }
        const char *password = ap->password();
        if (password) {
          if (strlen(password) == 64)  // it's not a passphrase, is the PSK
            memcpy(reinterpret_cast<char *>(conf.sta.password), password, 64);
          else
            strlcpy(reinterpret_cast<char *>(conf.sta.password), password,
                    sizeof(conf.sta.password));
        }
        // conf.sta.rm_enabled = 1;
        // conf.sta.btm_enabled = 1;
        // conf.sta.mbo_enabled = 1;
        conf.sta.pmf_cfg.capable = 1;

        wifi_config_t current_conf;
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_wifi_get_config(WIFI_IF_STA, &current_conf));
        if (!sta_config_equal(current_conf, conf)) {
          logI("setting STA config");
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              esp_wifi_set_config(WIFI_IF_STA, &conf));
        }
        ErrorList errl;
        _sta.apply(errl);
        /*_sta.stopDhcp();
        _sta.apply(Interface::ConfigItem::Ip, errl);
        _sta.apply(Interface::ConfigItem::Dns, errl);
        _sta.apply(Interface::ConfigItem::Role, errl);*/
        if (!errl.empty())
          return false;
        logI("connecting to %s [%s], channel %d", ssid, bssidStr,
             (int)_channel);
      }
      if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect()) != ESP_OK)
        return false;
      int i = 30;
      while (!isConnected() && --i) {
        delay(100);
        esp_task_wdt_reset();
      }
      i = 100;
      _errReason = (wifi_err_reason_t)0;
      while (!isConnected() && !_errReason && --i) {
        delay(100);
        esp_task_wdt_reset();
      }
      bool ok = isConnected();
      if (!ok) {
        logW("connection failed: %u", (int)_errReason);
        if (_ap && _ap->_status == ApStatus::Running)
          delay(5000);
        else
          delay(1000);
      }
      if (ap) {
        if (!ok)
          ap->failed();
        else
          ap->succeeded();
        addOrUpdateAp(ap->clone());
        config::Changed::publish(this);
      }
      return ok;
    }

    bool Wifi::tryConnect() {
      bool connected = false;
      if (_connect) {
        connected = tryConnect(_connect.get(), false);
        if (connected)
          _connect.reset();
      }
      if (!_ap || _ap->clientsCount() == 0) {
        if (!connected)
          for (auto ap : _aps)
            if (ap->bssid()) {
              connected = tryConnect(ap, false);
              if (connected)
                break;
            }
        if (!connected)
          for (auto ap : _aps) {
            connected = tryConnect(ap, true);
            if (connected)
              break;
          }
      }
      if (connected) {
        _sta.setStatus(StaStatus::Connected);
        _connectFailures = 0;
        // updateTimeConfig();
      } else {
        delay(100);
        if (!_ap || _ap->clientsCount() == 0)
          _connectFailures++;
      }
      return connected;
    }

    esp_err_t Wifi::mode(wifi_mode_t prev, wifi_mode_t next) {
      if (prev == next)
        return ESP_OK;
      logI("changing mode: %s -> %s", ModeNames[prev], ModeNames[next]);
      return ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(next));
    }

    Wifi &Wifi::instance() {
      static Wifi i;
      return i;
    }

    bool compareAps(ApInfo *i1, ApInfo *i2) {
      int f1 = i1->flags() & ApInfo::Flags::Fallback;
      int f2 = i2->flags() & ApInfo::Flags::Fallback;
      int i = f1 - f2;
      if (i == 0)
        i = i1->failcount() - i2->failcount();
      return i < 0;
    }

    ApInfo *Wifi::addOrUpdateAp(JsonArrayConst source, bool &changed) {
      if (!source || source.size() < 4)
        return nullptr;
      uint32_t id = 0;
      int i = 0;
      if (source[0].is<int>())
        id = source[i++];
      const char *ssid = source[i++];
      if (!ssid)
        return nullptr;
      const char *password = source[i++];
      auto flags = (ApInfo::Flags)source[i++].as<int>();
      int failcount = source[i++];
      const char *bssidStr = source[i++];
      uint8_t bssid[6];
      uint8_t *bssidPtr = net::macParse(bssidStr, bssid) ? bssid : nullptr;
      ApInfo *ap = new ApInfo(id, ssid, password, bssidPtr, flags, failcount);
      ApInfo *result = addOrUpdateAp(ap);
      if (!result->equals(ap))
        changed = true;
      return result;
    }

    void Wifi::ensureId(ApInfo *ap) {
      if (ap->id())
        return;
      uint32_t max = 0;
      for (auto it = _aps.begin(); it != _aps.end(); ++it)
        if ((*it)->id() > max)
          max = (*it)->id();
      ap->_id = max + 1;
    }

    ApInfo *Wifi::addOrUpdateAp(ApInfo *ap) {
      for (auto it = _aps.begin(); it != _aps.end(); ++it)
        if ((*it)->is(ap->id(), ap->ssid(), ap->bssid())) {
          auto pf = (*it)->flags() & ApInfo::Flags::Fallback;
          bool match = (*it)->matchesPassword(ap->password());
          if (!match) {
            delete *it;
            ensureId(ap);
            *it = ap;
          }
          (*it)->_flags = ap->flags() | pf;
          (*it)->_failcount = ap->failcount();
          if (match)
            delete ap;
          std::sort(_aps.begin(), _aps.end(), compareAps);
          return *it;
        }
      ensureId(ap);
      _aps.push_back(ap);
      if (_aps.size() > _maxAps) {
        auto it = _aps.rbegin();
        while (it != _aps.rend() && _aps.size() > _maxAps)
          if (!(*it)->isFallback()) {
            delete *it;
            it = decltype(it)(_aps.erase(std::next(it).base()));
          } else
            ++it;
      }
      std::sort(_aps.begin(), _aps.end(), compareAps);
      return ap;
    }

    void Wifi::run() {
      int arcf;
      esp_task_wdt_add(NULL);
      init();
      for (;;) {
        esp_task_wdt_reset();
        if (!_managedExternally) {
          auto curtime = millis();
          switch (_sta._status) {
            case StaStatus::Initial:
              if (_stopped)
                break;
              if (!_connect && !_aps.size()) {  // no APs to connect to
                if (_ap && _ap->_status == ApStatus::Initial) {
                  logW("no APs to connect to, switching to AP mode");
                  _ap->enable(true);
                }
                break;
              }
              _sta.setStatus(StaStatus::Connecting);
              _staTimer = curtime;
              break;
            case StaStatus::Connecting:
              if (!tryConnect()) {
                _sta.setStatus(StaStatus::ConnectionFailed);
                _staTimer = curtime;
              }
              break;
            case StaStatus::ConnectionFailed:
              arcf = _sta.options().autoRestartConnectFailures;
              if (arcf > 0 && (_connectFailures > arcf)) {
                logW("too many connection failures, restarting...");
                App::restart();
              }
              if (_ap && _ap->_status == ApStatus::Initial) {
                logW("connection failed, switching to AP+STA");
                _ap->enable(true);
              }
              if (_connect || (curtime - _staTimer > 10000 &&
                               (!_ap || _ap->clientsCount() == 0)))
                _sta.setStatus(StaStatus::Initial);
              break;
            case StaStatus::Connected:
              if (!isConnected())
                _sta.setStatus(StaStatus::Initial);
              break;
          }
          if (_ap)
            _ap->stateMachine();
        }
        checkScan();
        checkNameChanged();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      }
    }

    void Wifi::stop() {
      _stopped = true;
      if (_managedExternally)
        return;
      _sta.disconnect();
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
      waitState(
          [this] {
            return (xEventGroupGetBits(_eventGroup) & WifiFlags::StaRunning) ==
                   0;
          },
          1000);
    }

    void Wifi::checkScan() {
      if (!_pendingResponse)
        return;
      if (xEventGroupGetBits(_eventGroup) & WifiFlags::ScanDone) {
        _scanStarted = 0;
        uint16_t sc = 0;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_get_ap_num(&sc));

        DynamicJsonDocument *doc = nullptr;
        if (sc > 0) {
          doc = new DynamicJsonDocument(
              JSON_ARRAY_SIZE(sc) +
              (sc * (JSON_ARRAY_SIZE(5) + 33 + net::MacMaxChars)));
          JsonArray arr = doc->to<JsonArray>();
          wifi_ap_record_t *scanResult =
              (wifi_ap_record_t *)calloc(sc, sizeof(wifi_ap_record_t));
          if (doc && scanResult &&
              ESP_ERROR_CHECK_WITHOUT_ABORT(
                  esp_wifi_scan_get_ap_records(&sc, scanResult)) == ESP_OK) {
            for (uint16_t i = 0; i < sc; i++) {
              wifi_ap_record_t *r = scanResult + i;
              auto entry = arr.createNestedArray();
              if (entry) {
                entry.add(r->ssid);
                entry.add(r->authmode);
                entry.add(r->rssi);
                entry.add(r->primary);
                json::macTo(entry, r->bssid);
              }
            }
          }
          if (scanResult)
            free(scanResult);
          _pendingResponse->setData(doc);
        }
        _pendingResponse->publish();
        delete _pendingResponse;
        _pendingResponse = nullptr;
        xEventGroupClearBits(_eventGroup, WifiFlags::ScanDone);
        return;
      }
      auto curtime = millis();
      if (!_scanStarted || (curtime - _scanStarted > 15000)) {
        _scanStarted = curtime;
        xEventGroupClearBits(_eventGroup, WifiFlags::Scanning);
        esp_wifi_scan_stop();
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html
        // Currently, the esp_wifi_scan_start() API is supported only in station
        // or station/AP mode.
        _sta.enable(true);
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_wifi_scan_start(&_scanConfig, false)) == ESP_OK) {
          logI("scan started");
          xEventGroupSetBits(_eventGroup, WifiFlags::Scanning);
        }
      }
    }

    bool Wifi::pollSensors() {
      if (isConnected()) {
        wifi_ap_record_t info;
        if (!esp_wifi_sta_get_ap_info(&info)) {
          // sensor("rssi", info.rssi);
          _rssi.set(info.rssi);
        }
      }
      return true;
    };

    Wifi *useWifi() {
      return &Wifi::instance();
    }

    namespace wifi {
      void addAccessPoint(const char *ssid, const char *password,
                          const uint8_t *bssid) {
        Wifi::instance().addFallback(ssid, password, bssid);
      }
    }  // namespace wifi
  }    // namespace net
}  // namespace esp32m