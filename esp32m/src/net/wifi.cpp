#include "esp32m/net/wifi.hpp"
#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/events/diag.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/net/ip_event.hpp"
#include "esp32m/net/wifi_utils.hpp"

#include <dhcpserver/dhcpserver.h>
#include <dhcpserver/dhcpserver_options.h>
#include <esp_mac.h>
#include <esp_mbo.h>
#include <esp_netif.h>
#include <esp_netif_types.h>
#include <esp_rrm.h>
#include <esp_task_wdt.h>
#include <esp_wnm.h>
#include <lwip/apps/netbiosns.h>
#include <lwip/apps/sntp.h>
#include <lwip/dns.h>
#include <mdns.h>
#include <algorithm>

namespace esp32m {
  namespace net {
    const esp_ip4_addr_t DefaultNetmask = {
        PP_HTONL(LWIP_MAKEU32(255, 255, 255, 0))};
    const esp_ip4_addr_t DefaultApIp = {PP_HTONL(LWIP_MAKEU32(192, 168, 4, 1))};
    const char *DefaultNtpHost = "pool.ntp.org";

    const char *StaStateNames[]{
        "Initial",
        "Connecting",
        "Connected",
        "ConnectionFailed",
    };
    const char *ApStateNames[]{"Initial", "Starting", "Running", "Stopped"};

    const char *ModeNames[]{"Disabled", "STA", "AP", "AP+STA"};

    enum WifiFlags {
      Initialized = BIT0,
      StaRunning = BIT1,
      StaConnected = BIT2,
      StaGotIp = BIT3,
      ApRunning = BIT4,
      ScanDone = BIT5,
    };

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
      char bssidStr[18];
      if (formatBssid(bssid(), bssidStr))
        result = target.add(bssidStr);
      return result;
    }

    Wifi::Wifi()
        : Device(Flags::HasSensors),
          _apIp{DefaultApIp, DefaultNetmask, DefaultApIp},
          _ntpHost((char *)DefaultNtpHost) {
      /*_scanConfig.scan_time.active.min = 100;
      _scanConfig.scan_time.active.max = 300;*/
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
            parseBssid(data["bssid"].as<const char *>(), bssid) ? bssid
                                                                : nullptr));
        req.respond();
        if (isConnected())
          disconnect();
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
          App::instance().config()->save();
        req.respond(error);
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
                        WPA_GET_LE32(nr + ETH_ALEN));
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

    void Wifi::init() {
      static bool inited = false;
      if (inited)
        return;
      inited = true;
      EventManager::instance().subscribe([this](Event &ev) {
        IpEvent *ip;
        WifiEvent *wifi;
        sleep::Event *slev;
        if (WifiEvent::is(ev, &wifi))
          switch (wifi->event()) {
            case WIFI_EVENT_STA_START:
              xEventGroupSetBits(_eventGroup, WifiFlags::StaRunning);
              break;
            case WIFI_EVENT_STA_STOP:
              xEventGroupClearBits(_eventGroup, WifiFlags::StaRunning);
              break;
            case WIFI_EVENT_STA_CONNECTED:
              esp_wifi_set_rssi_threshold(-67);
              xEventGroupSetBits(_eventGroup, WifiFlags::StaConnected);
              break;
            case WIFI_EVENT_STA_DISCONNECTED: {
              auto r = (wifi_event_sta_disconnected_t *)wifi->data();
              _errReason = (wifi_err_reason_t)r->reason;
              xEventGroupClearBits(
                  _eventGroup, WifiFlags::StaConnected | WifiFlags::StaGotIp);
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
              logI("scan done");
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
              break;
          }
        else if (IpEvent::is(ev, &ip))
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
              break;
          }
        else if (sleep::Event::is(ev, &slev) && !isConnected())
          slev->block();

        if (_task)
          xTaskNotifyGive(_task);
      });
      // const char *hostname = App::instance().name();
      esp_netif_init();
      _eventGroup = xEventGroupCreate();
      ESP_ERROR_CHECK(esp_event_loop_create_default());
      _ifap = esp_netif_create_default_wifi_ap();
      _ifsta = esp_netif_create_default_wifi_sta();
      wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
      ESP_ERROR_CHECK(esp_wifi_init(&cfg));
      ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
      /*esp_netif_set_hostname(_ifsta, hostname);
      esp_netif_set_hostname(_ifap, hostname);*/
      ESP_ERROR_CHECK(esp_event_handler_instance_register(
          WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr, nullptr));
      ESP_ERROR_CHECK(esp_event_handler_instance_register(
          IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, nullptr, nullptr));
      xEventGroupSetBits(_eventGroup, WifiFlags::Initialized);
      ESP_ERROR_CHECK(esp_wifi_start());
      _errReason = (wifi_err_reason_t)0;
      WifiEvent::publish(WIFI_EVENT_WIFI_READY,
                         nullptr);  // this event is never sent by esp-idf, so
                                    // we do it in case someone wants it
      if (_txp)
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_max_tx_power(_txp));
      else
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_get_max_tx_power(&_txp));
      if (_task)
        xTaskNotifyGive(_task);
      ap(false);
    }

    esp_err_t Wifi::checkNameChanged() {
      if (!(_appNameChanged && isInitialized()))
        return ESP_OK;
      _appNameChanged = false;
      const char *hostname = App::instance().name();
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_hostname(_ifsta, hostname));
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_hostname(_ifap, hostname));
      if (ESP_ERROR_CHECK_WITHOUT_ABORT(mdns_init()) == ESP_OK)
        ESP_ERROR_CHECK_WITHOUT_ABORT(mdns_hostname_set(hostname));
      netbiosns_set_name(hostname);
      return ESP_OK;
    }

    bool Wifi::isInitialized() const {
      if (!_eventGroup)
        return false;
      return xEventGroupGetBits(_eventGroup) & WifiFlags::Initialized;
    }

    bool Wifi::isConnected() const {
      if (!_eventGroup)
        return false;
      return (xEventGroupGetBits(_eventGroup) &
              (WifiFlags::StaConnected | WifiFlags::StaGotIp)) ==
             (WifiFlags::StaConnected | WifiFlags::StaGotIp);
    }

    esp_err_t Wifi::disconnect() {
      if (xEventGroupGetBits(_eventGroup) & WifiFlags::StaConnected)
        return ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
      return ESP_OK;
    }

    bool Wifi::handleEvent(Event &ev) {
      DoneReason reason;
      if (EventInit::is(ev, 0)) {
        init();
        xTaskCreate([](void *self) { ((Wifi *)self)->run(); }, "m/wifi", 5120,
                    this, tskIDLE_PRIORITY + 1, &_task);
        return true;
      } else if (EventDone::is(ev, &reason)) {
        stop();
        return true;
      } else if (EventPropChanged::is(ev, App::PropName)) {
        _appNameChanged = true;
        if (_task)
          xTaskNotifyGive(_task);
      }
      return false;
    }

    DynamicJsonDocument *Wifi::getState(const JsonVariantConst filter) {
      size_t size = JSON_OBJECT_SIZE(
                        4 + 3)  // mode, ch, ch2, status + nested: wifi, sta, ap
                    + JSON_OBJECT_SIZE(8) + 33 + 18 + 18 + 16 + 16 +
                    16  // sta: ssid(33), bssid(18), mac(18), ip(16), gw(16),
                        // mask(16), rssi
                    + JSON_OBJECT_SIZE(6) + 18 + 16 + 16 +
                    16;  // ap: mac(18), ip(16), gw(16), mask(16), cli
      auto doc = new DynamicJsonDocument(size);
      auto info = doc->to<JsonObject>();
      wifi_mode_t wfm = WIFI_MODE_NULL;
      if (esp_wifi_get_mode(&wfm) == ESP_OK) {
        info["mode"] = (int)wfm;
        uint8_t pc;
        wifi_second_chan_t sc;
        if (esp_wifi_get_channel(&pc, &sc) == ESP_OK) {
          if (pc)
            info["ch"] = pc;
          if (sc)
            info["ch2"] = sc;
        }
        switch (wfm) {
          case WIFI_MODE_STA:
            staInfo(info);
            break;
          case WIFI_MODE_AP:
            apInfo(info);
            break;
          case WIFI_MODE_APSTA:
            apInfo(info);
            staInfo(info);
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

    DynamicJsonDocument *Wifi::getConfig(const JsonVariantConst args) {
      bool haveSta = _staDhcp || !ip4_addr_isany_val(_staIp.ip);
      bool haveAp = !ip4_addr_isany_val(_apIp.ip);
      bool haveTime = _useNtp || _ntpHost || _tzOfs || _dstOfs;
      size_t size = JSON_OBJECT_SIZE(1) +  // txp
                    (haveSta ? JSON_OBJECT_SIZE(1 + 7) + 3 * 16
                             : 0) +  // sta: dhcp, ip, gw, mask, proto, bw,
                                     // inact, strings(ip, gw, mask)
                    (haveAp ? JSON_OBJECT_SIZE(1 + 8) + 3 * 16
                            : 0) +  // ap:  ip, gw, mask, proto, bw, inact,
                                    // strings(ip, gw, mask)
                    (haveTime ? JSON_OBJECT_SIZE(1 + 4)
                              : 0);  // time:  use-ntp, ntp-host, tz, dst
      bool maskSensitive = config::getMaskSensitive(args);
      size_t apsCount = _aps.size();
      if (apsCount) {
        size += JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(apsCount);
        for (auto it = _aps.begin(); it != _aps.end(); it++)
          size += (*it)->jsonSize(maskSensitive);
      }

      auto doc = new DynamicJsonDocument(size);
      auto cr = doc->to<JsonObject>();
      cr["txp"] = _txp;
      if (haveSta) {
        auto sta = cr.createNestedObject("sta");
        if (_staDhcp)
          sta["dhcp"] = _staDhcp;
        ip2json(sta, _staIp);
        getIfcfg(WIFI_IF_STA, sta);
      }
      if (haveAp) {
        auto ap = cr.createNestedObject("ap");
        ip2json(ap, _apIp);
        getIfcfg(WIFI_IF_AP, ap);
      }
      if (haveTime) {
        auto time = cr.createNestedObject("time");
        if (_useNtp)
          time["ntp"] = _useNtp;
        if (_ntpHost)
          time["host"] = (const char *)_ntpHost;  // should be safe to reference
        if (_tzOfs)
          time["tz"] = _tzOfs;
        if (_dstOfs)
          time["dst"] = _dstOfs;
      }
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
      JsonObjectConst obj = ca["sta"];
      if (obj) {
        json::set(_staDhcp, obj["dhcp"], changed);
        json2ip(obj, _staIp, changed);
      }
      obj = ca["ap"];
      if (obj)
        json2ip(obj, _apIp, changed);
      obj = ca["time"];
      if (obj) {
        json::compareSet(_useNtp, obj["ntp"], changed);
        json::compareDup(_ntpHost, obj["host"], DefaultNtpHost, changed);
        json::compareSet(_tzOfs, obj["tz"], changed);
        json::compareSet(_dstOfs, obj["dst"], changed);
        updateTimeConfig();
      }
      JsonArrayConst aps = ca["aps"];
      if (aps)
        for (JsonArrayConst v : aps) addOrUpdateAp(v, changed);
      json::checkSetResult(err, result);
      return changed;
    }

    static bool sta_config_equal(const wifi_config_t &lhs,
                                 const wifi_config_t &rhs) {
      return memcmp(&lhs, &rhs, sizeof(wifi_config_t)) == 0;
    }

    bool Wifi::tryConnect(ApInfo *ap, bool noBssid) {
      esp_task_wdt_reset();
      disconnect();
      if (sta(true) != ESP_OK)
        return false;
      if (ap) {
        wifi_config_t conf;
        memset(&conf, 0, sizeof(wifi_config_t));
        const char *ssid = ap->ssid();
        strlcpy(reinterpret_cast<char *>(conf.sta.ssid), ssid,
                sizeof(conf.sta.ssid));
        const uint8_t *bssid = ap->bssid();
        char bssidStr[18] = "any";
        if (bssid && !noBssid) {
          conf.sta.bssid_set = 1;
          memcpy((void *)&conf.sta.bssid[0], bssid, 6);
          formatBssid(bssid, bssidStr);
        }
        const char *password = ap->password();
        if (password) {
          if (strlen(password) == 64)  // it's not a passphrase, is the PSK
            memcpy(reinterpret_cast<char *>(conf.sta.password), password, 64);
          else
            strlcpy(reinterpret_cast<char *>(conf.sta.password), password,
                    sizeof(conf.sta.password));
        }
        conf.sta.rm_enabled = 1;
        conf.sta.btm_enabled = 1;
        conf.sta.mbo_enabled = 1;
        conf.sta.pmf_cfg.capable = 1;

        wifi_config_t current_conf;
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_wifi_get_config(WIFI_IF_STA, &current_conf));
        if (!sta_config_equal(current_conf, conf)) {
          logI("setting STA config");
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              esp_wifi_set_config(WIFI_IF_STA, &conf));
        }
        esp_netif_dhcpc_stop(_ifsta);
        if (!_staDhcp && !ip4_addr_isany_val(_staIp.ip)) {
          if (ESP_ERROR_CHECK_WITHOUT_ABORT(
                  esp_netif_set_ip_info(_ifsta, &_staIp)) != ESP_OK)
            return false;
        } else if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcpc_start(
                       _ifsta)) == ESP_ERR_ESP_NETIF_DHCPC_START_FAILED)
          return false;
        logI("connecting to %s [%s]...", ssid, bssidStr);
      }

      if (!ip_addr_isany(&_dns1))
        dns_setserver(0, &_dns1);

      if (!ip_addr_isany(&_dns2))
        dns_setserver(1, &_dns2);

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
        if (_apState == ApState::Running)
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
        App::instance().config()->save();
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
      if (apClientsCount() == 0) {
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
        setState(StaState::Connected);
        _connectFailures = 0;
        updateTimeConfig();
      } else {
        delay(100);
        if (apClientsCount() == 0)
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

    esp_err_t Wifi::sta(bool enable) {
      wifi_mode_t m = WIFI_MODE_NULL;
      ESP_CHECK_RETURN(esp_wifi_get_mode(&m));
      return mode(m, enable ? (wifi_mode_t)(m | WIFI_MODE_STA)
                            : (wifi_mode_t)(m & ~WIFI_MODE_STA));
    }

    esp_err_t Wifi::ap(bool enable) {
      wifi_mode_t m = WIFI_MODE_NULL;
      ESP_CHECK_RETURN(esp_wifi_get_mode(&m));
      if (enable) {
        /*if (m & WIFI_MODE_AP)
          return ESP_OK;*/
        _apTimer = millis();
        // vTaskDelay(pdMS_TO_TICKS(100)); // allow some time for the
        // events to fire
        setState(ApState::Starting);
      }
      esp_err_t result = mode(m, enable ? (wifi_mode_t)(m | WIFI_MODE_AP)
                                        : (wifi_mode_t)(m & ~WIFI_MODE_AP));
      if (result != ESP_OK)
        return result;
      esp_netif_dhcps_stop(_ifap);
      if (enable) {
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_netif_set_ip_info(_ifap, &_apIp)) == ESP_OK) {
          dhcps_lease_t lease;
          lease.enable = true;
          lease.start_ip.addr =
              static_cast<uint32_t>(_apIp.ip.addr) + (1 << 24);
          lease.end_ip.addr = static_cast<uint32_t>(_apIp.ip.addr) + (11 << 24);

          ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(
              _ifap, ESP_NETIF_OP_SET,
              (esp_netif_dhcp_option_id_t)REQUESTED_IP_ADDRESS, (void *)&lease,
              sizeof(dhcps_lease_t)));
          esp_netif_dns_info_t dns;
          dns.ip.u_addr.ip4.addr = _apIp.ip.addr;
          dns.ip.type = IPADDR_TYPE_V4;
          dhcps_offer_t dhcps_dns_value = OFFER_DNS;
          ESP_ERROR_CHECK(esp_netif_dhcps_option(
              _ifap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
              &dhcps_dns_value, sizeof(dhcps_dns_value)));
          ESP_ERROR_CHECK(
              esp_netif_set_dns_info(_ifap, ESP_NETIF_DNS_MAIN, &dns));

          /*char ips[16];
      esp_ip4addr_ntoa(&_apIp.ip, ips, sizeof(ips));
      char url[16 + 11];
      auto ul = snprintf(url, sizeof(url), "http://%s/cp", ips);

      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(_ifap,
                                                           ESP_NETIF_OP_SET,
                                                           (esp_netif_dhcp_option_id_t)160,
      // see https://tools.ietf.org/html/rfc7710 url, ul));*/

          ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(_ifap));
        }
        wifi_config_t current_conf;
        ESP_CHECK_RETURN(esp_wifi_get_config(WIFI_IF_AP, &current_conf));

        wifi_config_t conf;
        memset(&conf, 0, sizeof(wifi_config_t));
        strlcpy(reinterpret_cast<char *>(conf.ap.ssid), App::instance().name(),
                sizeof(conf.ap.ssid));
        conf.ap.channel = 1;
        conf.ap.authmode = WIFI_AUTH_OPEN;
        conf.ap.ssid_len = strlen(reinterpret_cast<char *>(conf.ap.ssid));
        conf.ap.max_connection = 4;
        conf.ap.beacon_interval = 100;
        if (!sta_config_equal(current_conf, conf)) {
          logI("setting AP config");
          ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_AP, &conf));
        }
      } else
        setState(ApState::Stopped);
      return ESP_OK;
    }

    Wifi &Wifi::instance() {
      static Wifi i;
      return i;
    }

    void Wifi::updateTimeConfig() {
      if (isConnected()) {
        if (sntp_enabled())
          sntp_stop();
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(
            0, _useNtp ? (_ntpHost ? _ntpHost : DefaultNtpHost) : nullptr);
        sntp_init();
        char cst[17] = {0};
        char cdt[17] = "DST";
        char tz[33] = {0};
        long offset = _tzOfs * 60;
        int daylight = _dstOfs * 60;
        if (offset % 3600)
          sprintf(cst, "UTC%ld:%02u:%02u", (offset / 3600),
                  (uint32_t)abs((offset % 3600) / 60),
                  (uint32_t)abs(offset % 60));
        else
          sprintf(cst, "UTC%ld", offset / 3600);
        if (daylight != 3600) {
          long tz_dst = offset - daylight;
          if (tz_dst % 3600)
            sprintf(cdt, "DST%ld:%02u:%02u", tz_dst / 3600,
                    (uint32_t)abs((tz_dst % 3600) / 60),
                    (uint32_t)abs(tz_dst % 60));
          else
            sprintf(cdt, "DST%ld", tz_dst / 3600);
        }
        sprintf(tz, "%s%s", cst, cdt);
        setenv("TZ", tz, 1);
        tzset();
      }
    }

    void Wifi::setState(StaState state) {
      if (state == _staState)
        return;
      logI("Sta%s -> Sta%s", StaStateNames[(int)_staState],
           StaStateNames[(int)state]);
      int diagCode = -1;
      switch (state) {
        case StaState::Connecting:
          diagCode = DiagConnecting;
          break;
        case StaState::Connected:
          diagCode = DiagConnected;
          break;
        default:
          break;
      }
      if (diagCode > 0)
        event::Diag::publish(DiagId, diagCode);
      _staState = state;
    }

    void Wifi::setState(ApState state) {
      if (state == _apState)
        return;
      logI("Ap%s -> Ap%s", ApStateNames[(int)_apState],
           ApStateNames[(int)state]);
      int diagCode = -1;
      switch (state) {
        case ApState::Starting:
          diagCode = DiagApStarting;
          break;
        case ApState::Running:
          diagCode = DiagApRunning;
          break;
        default:
          break;
      }
      if (diagCode > 0)
        event::Diag::publish(DiagId, diagCode);
      _apState = state;
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
      uint8_t *bssidPtr = parseBssid(bssidStr, bssid) ? bssid : nullptr;
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
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        auto curtime = millis();
        switch (_staState) {
          case StaState::Initial:
            if (_stopped)
              break;
            if (!_connect && !_aps.size()) {  // no APs to connect to
              if (_apState == ApState::Initial) {
                logW("no APs to connect to, switching to AP mode");
                ap(true);
              }
              break;
            }
            setState(StaState::Connecting);
            _staTimer = curtime;
            break;
          case StaState::Connecting:
            if (!tryConnect()) {
              setState(StaState::ConnectionFailed);
              _staTimer = curtime;
            }
            break;
          case StaState::ConnectionFailed:
            if (_connectFailures > 10) {
              logW("too many connection failures, restarting...");
              App::restart();
            }
            if (_apState == ApState::Initial) {
              logW("connection failed, switching to AP+STA");
              ap(true);
            }
            if (_connect ||
                (curtime - _staTimer > 10000 && apClientsCount() == 0))
              setState(StaState::Initial);
            break;
          case StaState::Connected:
            if (!isConnected())
              setState(StaState::Initial);
            break;
        }
        switch (_apState) {
          case ApState::Starting:
            if (xEventGroupGetBits(_eventGroup) & WifiFlags::ApRunning) {
              setState(ApState::Running);
              _apTimer = curtime;
              break;
            }
            if (curtime - _apTimer <= 5000)
              break;
            logI("could not start AP, falling back to STA");
            setState(ApState::Initial);
            break;
          case ApState::Running: {
            /*if (!(xEventGroupGetBits(_eventGroup) & WifiFlags::ApRunning)) {
              // we can't rely on ApRunning status because AP_STOP/AP_START
            events may be fired multiple times during AP startup
              setState(ApState::Stopped);
              break;
            }*/
            if (apClientsCount() > 0)
              _apTimer = curtime;
            if (curtime - _apTimer >= 60000) {
              if (isConnected()) {
                logW("disabling AP because STA is connected");
                ap(false);
              } else {
                logW("no clients connected within 60s, restarting...");
                App::restart();
              }
              delay(100);  // allow some time for the events to fire
              break;
            }
            if (!_captivePortal)
              _captivePortal = new CaptiveDns(_apIp.ip);
            _captivePortal->enable(true);
          } break;
          case ApState::Stopped:
            if (_captivePortal)
              _captivePortal->enable(false);
            setState(ApState::Initial);
            break;
          default:
            break;
        };
        checkScan();
        checkNameChanged();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      }
    }

    int Wifi::apClientsCount() {
      if (!(xEventGroupGetBits(_eventGroup) & WifiFlags::ApRunning))
        return 0;
      wifi_sta_list_t clients;
      if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_ap_get_sta_list(&clients)) !=
          ESP_OK)
        return 0;
      return clients.num;
    }

    void Wifi::stop() {
      _stopped = true;
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
          doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(sc) +
                                        (sc * (JSON_ARRAY_SIZE(5) + 33 + 18)));
          JsonArray arr = doc->to<JsonArray>();
          wifi_ap_record_t *scanResult =
              (wifi_ap_record_t *)calloc(sc, sizeof(wifi_ap_record_t));
          if (doc && scanResult &&
              ESP_ERROR_CHECK_WITHOUT_ABORT(
                  esp_wifi_scan_get_ap_records(&sc, scanResult)) == ESP_OK) {
            char sbssid[18] = {0};
            for (uint16_t i = 0; i < sc; i++) {
              wifi_ap_record_t *r = scanResult + i;
              auto entry = arr.createNestedArray();
              if (entry) {
                entry.add(r->ssid);
                entry.add(r->authmode);
                entry.add(r->rssi);
                entry.add(r->primary);
                if (formatBssid(r->bssid, sbssid))
                  entry.add(sbssid);
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
        esp_wifi_scan_stop();
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_wifi_scan_start(&_scanConfig, false)) == ESP_OK)
          logI("scan started");
      }
    }

    bool Wifi::pollSensors() {
      if (isConnected()) {
        wifi_ap_record_t info;
        if (!esp_wifi_sta_get_ap_info(&info))
          sensor("rssi", info.rssi);
      }
      return true;
    };

    void Wifi::staInfo(JsonObject info) {
      if (isConnected()) {
        auto infoSta = info.createNestedObject("sta");
        wifi_ap_record_t info;
        if (!esp_wifi_sta_get_ap_info(&info)) {
          char sbssid[18] = {0};
          infoSta["ssid"] = info.ssid;
          if (formatBssid(info.bssid, sbssid))
            infoSta["bssid"] = sbssid;
          uint8_t mac[6];
          if (!esp_wifi_get_mac(WIFI_IF_STA, mac) && formatBssid(mac, sbssid))
            infoSta["mac"] = sbssid;
          infoSta["rssi"] = info.rssi;
        }
        esp_netif_ip_info_t ip;
        if (!ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_get_ip_info(_ifsta, &ip)))
          ip2json(infoSta, ip);
      }
    }

    void Wifi::apInfo(JsonObject info) {
      auto infoAp = info.createNestedObject("ap");
      const char *hostname = NULL;
      if (!esp_netif_get_hostname(_ifap, &hostname))
        infoAp["ssid"] = (char *)hostname;
      uint8_t mac[6];
      char sbssid[18] = {0};
      if (!esp_wifi_get_mac(WIFI_IF_AP, mac) && formatBssid(mac, sbssid))
        infoAp["mac"] = sbssid;
      wifi_sta_list_t clients;
      if (!esp_wifi_ap_get_sta_list(&clients))
        infoAp["cli"] = clients.num;

      esp_netif_ip_info_t ip;
      if (!ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_get_ip_info(_ifap, &ip)))
        ip2json(infoAp, ip);
    }

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