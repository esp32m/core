#include "esp32m/net/wifi.hpp"
#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/events/diag.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/integrations/ha/ha.hpp"
#include "esp32m/net/net.hpp"
#include "esp32m/net/ota.hpp"
#include "esp32m/props.hpp"

#if CONFIG_ESP32M_BOARD_TYPE_XIAO_ESP32C6
#  include <esp32m/io/gpio.hpp>
#endif

// #include <dhcpserver/dhcpserver.h>
// #include <dhcpserver/dhcpserver_options.h>
#include <esp_mac.h>
#include <esp_mbo.h>
#include <esp_netif.h>
#include <esp_netif_types.h>
#include <esp_rrm.h>
#include <esp_task_wdt.h>
#include <esp_wnm.h>
#include <esp_wps.h>
#include <lwip/dns.h>
#include <algorithm>

#include <sdkconfig.h>

namespace esp32m {
  namespace net {

    static bool sta_config_equal(const wifi_config_t& lhs,
                                 const wifi_config_t& rhs) {
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
      // Event handlers post notifications; roaming policy and scan ownership
      // are updated only by the m/wifi worker.
      RoamConnected = BIT7,
      RoamDisconnected = BIT8,
      RoamAssociation = BIT9,
      RoamNeighbor = BIT10,
    };

    namespace wifi {

      Sta::Sta() {
        _role = Role::DhcpClient;
      };

      void Sta::init(net::Wifi* wifi, const char* key) {
        auto handle = esp_netif_get_handle_from_ifkey(key);
        if (handle) {
          auto flags = esp_netif_get_flags(handle);
          if ((flags & ESP_NETIF_DHCP_CLIENT) == 0)
            _role = Role::Default;
        }
        Iface::init(wifi, key);
      }

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

      esp_err_t Sta::startWPS() {
        esp_wps_config_t config = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
        ESP_CHECK_RETURN(enable(true));
        ESP_CHECK_RETURN(esp_wifi_wps_enable(&config));
        ESP_CHECK_RETURN(esp_wifi_wps_start());
        logI("WPS started");
        return ESP_OK;
      }

      void Sta::getInfo(JsonObject info) {
        if (isConnected()) {
          auto infoSta = info["sta"].to<JsonObject>();
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

      const char* StaStatusNames[]{
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

      void Ap::init(net::Wifi* wifi, const char* key) {
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
          strlcpy(reinterpret_cast<char*>(conf.ap.ssid),
                  App::instance().hostname(), sizeof(conf.ap.ssid));
          conf.ap.channel = 1;
          conf.ap.authmode = WIFI_AUTH_OPEN;
          conf.ap.ssid_len = strlen(reinterpret_cast<char*>(conf.ap.ssid));
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
        auto infoAp = info["ap"].to<JsonObject>();
        const char* hostname = NULL;
        if (!esp_netif_get_hostname(handle(), &hostname))
          infoAp["ssid"] = (char*)hostname;
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

      const char* ApStateNames[]{"initial", "starting", "running", "stopped"};

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

      static const char* getDefaultStaKey() {
        const esp_netif_inherent_config_t config =
            ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
        return config.if_key;
      }

#ifdef CONFIG_ESP_WIFI_SOFTAP_SUPPORT
      static const char* getDefaultApKey() {
        const esp_netif_inherent_config_t config =
            ESP_NETIF_INHERENT_DEFAULT_WIFI_AP();
        return config.if_key;
      }
#endif
    }  // namespace wifi

    const char* ModeNames[]{"Disabled", "STA", "AP", "AP+STA"};

    bool WifiEvent::is(wifi_event_t event) const {
      return _event == event;
    }

    bool WifiEvent::is(Event& ev, WifiEvent** r) {
      if (!ev.is(Type))
        return false;
      if (r)
        *r = (WifiEvent*)&ev;
      return true;
    }

    bool WifiEvent::is(Event& ev, wifi_event_t event, WifiEvent** r) {
      if (!ev.is(Type))
        return false;
      wifi_event_t t = ((WifiEvent&)ev)._event;
      if (t != event)
        return false;
      if (r)
        *r = (WifiEvent*)&ev;
      return true;
    }

    void WifiEvent::publish(wifi_event_t event, void* data) {
      WifiEvent ev(event, data);
      EventManager::instance().publish(ev);
    }

    ApInfo::ApInfo(uint32_t id, const char* ssid, const char* password,
                   const uint8_t* bssid, Flags flags, uint8_t failcount) {
      _id = id;
      auto sl = ssid ? strlen(ssid) : 0;
      auto pl = password ? strlen(password) : 0;
      _buffer = (char*)malloc(sl + pl + 2 + (bssid ? 6 : 0));
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
    bool ApInfo::is(uint32_t id, const char* ssid, const uint8_t* bssid) {
      if (id)
        return id == _id;
      const char* mySsid = this->ssid();
      if (!ssid && strlen(mySsid))
        return false;
      if (strcmp(ssid, mySsid))
        return false;
      if (!bssid)
        return true;
      return _bssidOfs && !memcmp(bssid, this->bssid(), 6);
    }
    bool ApInfo::matchesPassword(const char* pass) {
      auto pl = pass ? strlen(pass) : 0;
      if (pl != strlen(password()))
        return false;
      if (!pl)
        return true;
      return !strcmp(pass, password());
    }

    bool ApInfo::equals(ApInfo* other) {
      if (this == other)
        return true;
      if (_id != other->_id || _flags != other->_flags ||
          _failcount != other->_failcount)
        return false;
      if (strcmp(ssid(), other->ssid()) ||
          strcmp(password(), other->password()))
        return false;
      if (_bssidOfs != other->_bssidOfs)
        return false;
      if (!_bssidOfs)
        return true;
      return !memcmp(bssid(), other->bssid(), 6);
    }

    /*size_t ApInfo::jsonSize(bool maskPassword) {
      return JSON_ARRAY_SIZE(5 + (_bssidOfs ? 1 : 0)) + strlen(ssid()) + 1 +
             (maskPassword ? 0 : (strlen(password()) + 1)) +
             (_bssidOfs ? 19 : 0);
    }*/

    bool ApInfo::toJson(JsonArray& target, bool maskPassword) {
      auto result = target.add(_id) && target.add((char*)ssid()) &&
                    (maskPassword ? target.add(nullptr)
                                  : target.add((char*)password())) &&
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

    bool Wifi::handleRequest(Request& req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("scan")) {
        xEventGroupClearBits(_eventGroup, WifiFlags::ScanDone);
        _pendingResponse = req.makeResponse();
        xTaskNotifyGive(_task);
        return true;
      }
      if (req.is("wps")) {
        auto error = sta().startWPS();
        req.respond(error);
        return true;
      }
      if (req.is("connect")) {
        auto data = req.data();
        uint8_t bssid[6];
        _connect = std::unique_ptr<ApInfo>(new ApInfo(
            0, data["ssid"].as<const char*>(),
            data["password"].as<const char*>(),
            net::macParse(data["bssid"].as<const char*>(), bssid) ? bssid
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

    void wifi_event_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data) {
      WifiEvent::publish((wifi_event_t)event_id, event_data);
    }

    void ip_event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
      auto wifi = (Wifi*)arg;
      IpEvent::publish(wifi->sta().handle(), (ip_event_t)event_id, event_data);
    }

    esp_err_t Wifi::init() {
      static bool inited = false;
      if (inited)
        return ESP_OK;
      inited = true;

      esp_netif_t *ifsta = nullptr, *ifap = nullptr;
      const char* staKey = getDefaultStaKey();

      ifsta = esp_netif_get_handle_from_ifkey(staKey);
#ifdef CONFIG_ESP_WIFI_SOFTAP_SUPPORT
      ifap = esp_netif_get_handle_from_ifkey(getDefaultApKey());
#endif
      // no need to fire net::IfEventType::Created because we subclass
      // net::IFace and call init()
      if (!ifsta) {
        IfEvent ev(staKey, IfEventType::Creating);
        ev.Event::publish();
        auto netif = ev.getNetif();
        ifsta = netif ? netif : esp_netif_create_default_wifi_sta();
      }
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
          WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, this, nullptr));
      ESP_CHECK_RETURN(esp_event_handler_instance_register(
          IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, this, nullptr));
      ESP_CHECK_RETURN(esp_event_handler_instance_register(
          IP_EVENT, IP_EVENT_STA_LOST_IP, ip_event_handler, this, nullptr));
      xEventGroupSetBits(_eventGroup, WifiFlags::Initialized);
      if (!_managedExternally)
        ESP_CHECK_RETURN(esp_wifi_start());
      _errReason = (wifi_err_reason_t)0;
      /*
      WifiEvent::publish(WIFI_EVENT_WIFI_READY,
                         nullptr);  // this event is never sent by esp-idf, so
                                    // we do it in case someone wants it
      */
      if (_delayedConfig) {
        ConfigApply req(json::emptyObject());
        RequestContext rc(req, _delayedConfig->as<JsonVariantConst>());
        setConfig(rc);
        _delayedConfig.reset();
      }

      _sta.init(this, esp_netif_get_ifkey(ifsta));

#ifdef CONFIG_ESP_WIFI_SOFTAP_SUPPORT
      _ap.reset(new Ap());
      _ap->init(this, esp_netif_get_ifkey(ifap));
      _ap->enable(false);
#endif

      if (_txp)
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_max_tx_power(_txp));
      else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_get_max_tx_power(&_txp));
        // Normalize to the quantized representative values per ESP-IDF mapping table:
        // {set range, actual} = {[80,84]→80, [72,79]→72, [66,71]→66, [60,65]→60,
        //  [56,59]→56, [52,55]→52, [44,51]→44, [34,43]→34, [28,33]→28, [20,27]→20, [8,19]→8}
        static constexpr int8_t kTxpLevels[] = {80, 72, 66, 60, 56, 52, 44, 34, 28, 20, 8};
        for (auto l : kTxpLevels)
          if (_txp >= l) {
            _txp = l;
            break;
          }
      }
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

    void Wifi::handleEvent(Event& ev) {
      Device::handleEvent(ev);
      IpEvent* ip;
      WifiEvent* wifi;
      sleep::Event* slev;
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

        xTaskCreate([](void* self) { ((Wifi*)self)->run(); }, "m/wifi", 5120,
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
          case WIFI_EVENT_STA_CONNECTED: {
            xEventGroupClearBits(_eventGroup, WifiFlags::RoamDisconnected |
                                                WifiFlags::RoamAssociation |
                                                WifiFlags::RoamNeighbor);
            xEventGroupSetBits(_eventGroup, WifiFlags::RoamConnected);
            if (_rssiThreshold)
              ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_rssi_threshold(_rssiThreshold));
            auto* sc = (wifi_event_sta_connected_t*)wifi->data();
            if (sc) {
              bool hadBssid = _lastBssid[0] | _lastBssid[1] | _lastBssid[2] |
                              _lastBssid[3] | _lastBssid[4] | _lastBssid[5];
              if (hadBssid && memcmp(_lastBssid, sc->bssid, 6) != 0)
                logI("roamed: " MACSTR " -> " MACSTR " ch=%u",
                     MAC2STR(_lastBssid), MAC2STR(sc->bssid), sc->channel);
              else
                logI("connected: bssid=" MACSTR " ch=%u",
                     MAC2STR(sc->bssid), sc->channel);
              memcpy(_lastBssid, sc->bssid, sizeof(_lastBssid));
            }
            xEventGroupSetBits(_eventGroup, WifiFlags::StaConnected);
#if CONFIG_LWIP_IPV6_AUTOCONFIG
            if (_sta.role() == Interface::Role::DhcpClient)
              esp_netif_create_ip6_linklocal(_sta.handle());
#endif
          } break;
          case WIFI_EVENT_STA_DISCONNECTED: {
            auto r = (wifi_event_sta_disconnected_t*)wifi->data();
            _errReason = (wifi_err_reason_t)r->reason;
            xEventGroupClearBits(_eventGroup,
                                 WifiFlags::StaConnected | WifiFlags::StaGotIp);
            xEventGroupClearBits(_eventGroup, WifiFlags::RoamConnected |
                                                WifiFlags::RoamAssociation |
                                                WifiFlags::RoamNeighbor);
            xEventGroupSetBits(_eventGroup, WifiFlags::RoamDisconnected);
            if (_errReason == WIFI_REASON_ROAMING) {
              xEventGroupSetBits(_eventGroup, WifiFlags::RoamAssociation);
              logI("roaming: leaving bssid=" MACSTR " rssi=%d",
                   MAC2STR(r->bssid), (int)r->rssi);
            } else
              logW("STA disconnected: bssid=" MACSTR " rssi=%d reason=%u",
                   MAC2STR(r->bssid), (int)r->rssi, (unsigned)_errReason);
            break;
          }
          case WIFI_EVENT_STA_BSS_RSSI_LOW: {
            auto* re = (wifi_event_bss_rssi_low_t*)wifi->data();
            logI("RSSI low (%d dBm), scheduling roaming check",
                 re ? (int)re->rssi : 0);
          } break;
#if CONFIG_WPA_11KV_SUPPORT
          case WIFI_EVENT_STA_NEIGHBOR_REP: {
            auto* nr = (wifi_event_neighbor_report_t*)wifi->data();
            logI("neighbor report received, len=%u",
                 nr ? (unsigned)nr->report_len : 0u);
            // NULL/empty reports also complete the request. The worker asks
            // the AP for its BTM candidate list and has a timeout fallback.
            xEventGroupSetBits(_eventGroup, WifiFlags::RoamNeighbor);
          } break;
#endif
          case WIFI_EVENT_STA_WPS_ER_SUCCESS: {
            logW("WPS succeeded");
            wifi_event_sta_wps_er_success_t* evt =
                (wifi_event_sta_wps_er_success_t*)wifi->data();
            int i;

            if (evt) {
              for (i = 0; i < evt->ap_cred_cnt; i++) {
                auto ssid = (const char*)evt->ap_cred[i].ssid;
                auto passphrase = (const char*)evt->ap_cred[i].passphrase;
                auto ap = new ApInfo(0, ssid, passphrase, nullptr);
                logI("got WPS credentials: %s (%s)", ssid, passphrase);
                if (_connect)
                  addOrUpdateAp(ap);
                else
                  _connect = std::unique_ptr<ApInfo>(ap);
              }
            } else {
              wifi_config_t current_conf;
              esp_wifi_get_config(WIFI_IF_STA, &current_conf);
              auto ssid = (const char*)current_conf.sta.ssid;
              if (strlen(ssid)) {
                auto passphrase = (const char*)current_conf.sta.password;
                logI("got WPS credentials: %s (%s)", ssid, passphrase);
                auto ap = new ApInfo(0, ssid, passphrase, nullptr);
                if (_connect)
                  addOrUpdateAp(ap);
                else
                  _connect = std::unique_ptr<ApInfo>(ap);
              }
            }

            _sta.disconnect();
            xTaskNotifyGive(_task);

            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_wps_disable());
          } break;
          case WIFI_EVENT_STA_WPS_ER_FAILED:
            logW("WPS failed");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_wps_disable());
            break;
          case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
            logW("WPS timed out");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_wps_disable());
            break;
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

    JsonDocument* Wifi::getState(RequestContext& ctx) {
      /*      size_t size =
                JSON_OBJECT_SIZE(4 + 3)  // mode, flags, ch, ch2  + nested: sta,
         ap
                + JSON_OBJECT_SIZE(8) + 33 + net::MacMaxChars * 2 + 16 + 16 +
                16  // sta: ssid(33), bssid(18), mac(18), ip(16),
                    // gw(16), mask(16), rssi
                + JSON_OBJECT_SIZE(6) + net::MacMaxChars + 16 + 16 +
                16;  // ap: mac(18), ip(16), gw(16), mask(16), cli*/
      auto doc = new JsonDocument(); /* size */
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

    JsonDocument* Wifi::getConfig(RequestContext& ctx) {
      bool maskSensitive = !ctx.request.isInternal();
      size_t apsCount = _aps.size();

      auto doc = new JsonDocument(); /* size */
      auto cr = doc->to<JsonObject>();
      cr["txp"] = _txp;
      cr["channel"] = _channel;
      cr["rssiThreshold"] = _rssiThreshold;
#if CONFIG_ESP32M_BOARD_TYPE_XIAO_ESP32C6
      cr["xiaoRFExt"] = _xiaoRFExt;
#endif
      auto sta = cr["sta"].to<JsonObject>();
      getIfcfg(WIFI_IF_STA, sta);
      auto ap = cr["ap"].to<JsonObject>();
      getIfcfg(WIFI_IF_AP, ap);
      if (apsCount) {
        auto aps = cr["aps"].to<JsonArray>();
        for (auto it = _aps.begin(); it != _aps.end(); it++) {
          JsonArray item = aps.add<JsonArray>();
          (*it)->toJson(item, maskSensitive);
        }
      }
      return doc;
    }

    bool Wifi::setConfig(RequestContext& ctx) {
      JsonObjectConst ca = ctx.data.as<JsonObjectConst>();
      if (!isInitialized()) {
        JsonDocument* doc = new JsonDocument();
        doc->set(ca);
        _delayedConfig.reset(doc);
        return false;
      }

      int8_t txp = ca["txp"];
      bool changed = false;
      if (txp != _txp)
        if (!txp || ctx.errors.check(ESP_ERROR_CHECK_WITHOUT_ABORT(
                                         esp_wifi_set_max_tx_power(txp)),
                                     "set tx power to %d", (int)txp) == ESP_OK) {
          _txp = txp;
          changed = true;
        }
      json::from(ca["channel"], _channel, &changed);
      {
        int8_t prev = _rssiThreshold;
        if (json::from(ca["rssiThreshold"], _rssiThreshold, &changed) &&
            isConnected() &&
            ctx.errors.check(ESP_ERROR_CHECK_WITHOUT_ABORT(
                                 esp_wifi_set_rssi_threshold(_rssiThreshold)),
                             "set RSSI threshold to %d", (int)_rssiThreshold) != ESP_OK)
          _rssiThreshold = prev;
      }
#if CONFIG_ESP32M_BOARD_TYPE_XIAO_ESP32C6
      if (json::from(ca["xiaoRFExt"], _xiaoRFExt, &changed)) {
        auto rfSwitchDisable = gpio::pin(GPIO_NUM_3)->digital();
        rfSwitchDisable->setDirection(false, true);
        rfSwitchDisable->write(false);  // enable RF switch
        auto rfUseUFL = gpio::pin(GPIO_NUM_14)->digital();
        rfUseUFL->setDirection(false, true);
        rfUseUFL->write(_xiaoRFExt);  // use UFL connector
      }
#endif
      JsonObjectConst obj = ca["sta"];
      if (obj)
        setIfcfg(WIFI_IF_STA, obj);

      obj = ca["ap"];
      if (obj)
        setIfcfg(WIFI_IF_AP, obj);

      JsonArrayConst aps = ca["aps"];
      if (aps)
        for (JsonArrayConst v : aps) addOrUpdateAp(v, changed);
      return changed;
    }

    bool Wifi::tryConnect(ApInfo* ap, bool noBssid, bool remember) {
      esp_task_wdt_reset();
      _sta.disconnect();
      if (!waitState([this] { return !_sta.isConnected(); }, 1000))
        return false;
      if (_sta.enable(true) != ESP_OK) {
        return false;
      }
      if (ap) {
        wifi_config_t conf;
        memset(&conf, 0, sizeof(wifi_config_t));
        const char* ssid = ap->ssid();
        strlcpy(reinterpret_cast<char*>(conf.sta.ssid), ssid,
                sizeof(conf.sta.ssid));

        conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        conf.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        conf.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        const uint8_t* bssid = ap->bssid();
        char bssidStr[net::MacMaxChars] = "any";
        if (_channel)
          conf.sta.channel = _channel;
        if (bssid && !noBssid) {
          conf.sta.bssid_set = 1;
          memcpy((void*)&conf.sta.bssid[0], bssid, 6);
          sprintf(bssidStr, MACSTR, MAC2STR(bssid));
        }
        const char* password = ap->password();
        if (password) {
          if (strlen(password) == 64)  // it's not a passphrase, is the PSK
            memcpy(reinterpret_cast<char*>(conf.sta.password), password, 64);
          else
            strlcpy(reinterpret_cast<char*>(conf.sta.password), password,
                    sizeof(conf.sta.password));
        }
        // PMF left as not-capable: setting capable=true causes ESP-IDF's WPA
        // supplicant to call esp_wifi_set_config() internally at the end of
        // the 4-way EAPOL handshake (PMKSA caching) while the driver state
        // machine still holds the "connecting" lock, producing a harmless but
        // noisy "sta is connecting, cannot set config" warning.
        // 802.11k and 802.11v work without PMF; only MBO mandates it, which is
        // why mbo_enabled is also omitted here.
        conf.sta.pmf_cfg.capable = false;
        conf.sta.pmf_cfg.required = false;
        // Enable 802.11k (RRM) and 802.11v (BTM) so the WPA supplicant
        // advertises support to the AP and the RSSI-low handler
        // (WIFI_EVENT_STA_BSS_RSSI_LOW) can drive seamless roaming.
#if CONFIG_ESP_WIFI_11R_SUPPORT || CONFIG_WPA_11R_SUPPORT
        conf.sta.ft_enabled = 1;
#endif
#if CONFIG_ESP_WIFI_11KV_SUPPORT || CONFIG_WPA_11KV_SUPPORT
        conf.sta.rm_enabled = 1;
        // IDF clears explicit BSSID/channel constraints when BTM is enabled.
        // Preserve the user's pinned first association; the worker still
        // provides bounded stronger-AP selection for pinned connections.
        conf.sta.btm_enabled = conf.sta.bssid_set ? 0 : 1;
#endif

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
        if (!errl.check(this))
          return false;
        logI("connecting to %s [%s], channel %d", ssid, bssidStr,
             (int)_channel);
      }
      _errReason = (wifi_err_reason_t)0;
      if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect()) != ESP_OK)
        return false;
      int i = 30;
      while (!isConnected() && --i) {
        if (!remember && (_connect || _stopped || ota::isRunning()))
          break;
        delay(100);
        esp_task_wdt_reset();
      }
      i = 100;
      while (!isConnected() && !_errReason && --i) {
        if (!remember && (_connect || _stopped || ota::isRunning()))
          break;
        delay(100);
        esp_task_wdt_reset();
      }
      bool ok = isConnected();
      if (!ok) {
        esp_netif_dhcp_status_t dhcpc = ESP_NETIF_DHCP_INIT;
        (void)esp_netif_dhcpc_get_status(_sta.handle(), &dhcpc);
        esp_netif_ip_info_t ip = {};
        (void)esp_netif_get_ip_info(_sta.handle(), &ip);
        auto bits = xEventGroupGetBits(_eventGroup);
        if ((bits & WifiFlags::StaConnected) && !(bits & WifiFlags::StaGotIp) &&
            !_errReason)
          logW("connected but failed to obtain IP (dhcpc=%d, ip=" IPSTR ")",
               (int)dhcpc, IP2STR(&ip.ip));
        else
          logW("connection failed: %u (dhcpc=%d, ip=" IPSTR ")",
               (int)_errReason, (int)dhcpc, IP2STR(&ip.ip));
        if (_ap && _ap->_status == ApStatus::Running)
          delay(5000);
        else
          delay(1000);
      }
      if (ap && remember) {
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
        bool hasPinnedBssid = false;
        for (auto ap : _aps)
          if (ap->bssid()) {
            hasPinnedBssid = true;
            break;
          }

        // If the user pinned one or more BSSIDs, always prefer them first.
        // Only fall back to the strongest-AP selection when none of the
        // pinned BSSIDs are reachable.
        if (!connected && hasPinnedBssid)
          for (auto ap : _aps)
            if (ap->bssid()) {
              connected = tryConnect(ap, false);
              if (connected)
                break;
            }

        // If no BSSID was configured (or all pinned BSSIDs failed), let the
        // stack choose the strongest AP for the SSID.
        if (!connected && hasPinnedBssid)
          logW("all pinned BSSIDs failed; falling back to strongest-AP selection");
        if (!connected)
          for (auto ap : _aps) {
            connected = tryConnect(ap, true);
            if (connected)
              break;
          }
      }
      if (connected) {
        // A successful saved fallback also completes the one-shot manual
        // intent; retaining it would suppress background roaming forever.
        _connect.reset();
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

    Wifi& Wifi::instance() {
      static Wifi i;
      return i;
    }

    bool compareAps(ApInfo* i1, ApInfo* i2) {
      int f1 = i1->flags() & ApInfo::Flags::Fallback;
      int f2 = i2->flags() & ApInfo::Flags::Fallback;
      int i = f1 - f2;
      if (i == 0)
        i = i1->failcount() - i2->failcount();
      return i < 0;
    }

    ApInfo* Wifi::addOrUpdateAp(JsonArrayConst source, bool& changed) {
      if (!source || source.size() < 4)
        return nullptr;
      uint32_t id = 0;
      int i = 0;
      if (source[0].is<int>())
        id = source[i++];
      const char* ssid = source[i++];
      if (!ssid)
        return nullptr;
      const char* password = source[i++];
      auto flags = (ApInfo::Flags)source[i++].as<int>();
      int failcount = source[i++];
      const char* bssidStr = source[i++];
      uint8_t bssid[6];
      uint8_t* bssidPtr = net::macParse(bssidStr, bssid) ? bssid : nullptr;
      ApInfo* ap = new ApInfo(id, ssid, password, bssidPtr, flags, failcount);
      ApInfo* result = addOrUpdateAp(ap);
      if (!result->equals(ap))
        changed = true;
      return result;
    }

    void Wifi::ensureId(ApInfo* ap) {
      if (ap->id())
        return;
      uint32_t max = 0;
      for (auto it = _aps.begin(); it != _aps.end(); ++it)
        if ((*it)->id() > max)
          max = (*it)->id();
      ap->_id = max + 1;
    }

    ApInfo* Wifi::addOrUpdateAp(ApInfo* ap) {
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
        checkRoaming();
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
              if (!isConnected() && !_roaming.associationPending(millis())) {
                // Cancel a stuck driver association too: Sta::disconnect()
                // intentionally only disconnects an established link.
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
                _sta.setStatus(StaStatus::Initial);
              }
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

    void Wifi::checkRoaming() {
      const uint32_t now = millis();
      const auto events = xEventGroupClearBits(
          _eventGroup, WifiFlags::RoamConnected | WifiFlags::RoamDisconnected |
                           WifiFlags::RoamNeighbor);
      if (events & WifiFlags::RoamDisconnected)
        _roaming.disconnected(events & WifiFlags::RoamAssociation, now);
      if (events & WifiFlags::RoamConnected) {
        _roaming.connected(now);
        // CONNECTED is a layer-2 event; DHCP may complete later. Keep the
        // bounded association grace until IP arrives instead of cancelling
        // a successful transition on the next worker iteration.
        if (!isConnected())
          _roaming.disconnected(true, now);
      }
      if (events & WifiFlags::RoamNeighbor)
        _roaming.neighborReport(now);

      const bool allowed = !_managedExternally && !_stopped && !_connect &&
                           !ota::isRunning() && _rssiThreshold &&
                           (!_ap || _ap->clientsCount() == 0);
      if (_roamScan) {
        if (events & WifiFlags::ScanDone) {
          finishRoamingScan();
        } else if (!allowed || uint32_t(now - _roamScanStarted) >= 15000) {
          esp_wifi_scan_stop();
          esp_wifi_clear_ap_list();
          xEventGroupClearBits(_eventGroup,
                              WifiFlags::Scanning | WifiFlags::ScanDone);
          _roamScan = false;
          _roaming.scanComplete(now);
        }
        return;
      }

      wifi_ap_record_t current = {};
      const bool connected = isConnected();
      const bool weak = connected &&
                        esp_wifi_sta_get_ap_info(&current) == ESP_OK &&
                        current.rssi < _rssiThreshold;
      // A manual UI scan owns the driver's one shared result list. Defer the
      // background scan until it has consumed that list; never stop its scan.
      const bool scanAvailable = !_pendingResponse &&
                                 !(events & WifiFlags::Scanning) &&
                                 (!_ap || _ap->clientsCount() == 0);
      switch (_roaming.poll(now, allowed, connected, weak, scanAvailable)) {
        case wifi::RoamingPolicy::Action::RequestNeighbors:
          logI("weak signal %d dBm; checking stronger APs", (int)current.rssi);
          // IDF's RSSI notification is one-shot. Rearm once per bounded
          // attempt; polling also catches links already weak at association.
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              esp_wifi_set_rssi_threshold(_rssiThreshold));
          if (esp_rrm_send_neighbor_report_request() < 0)
            _roaming.neighborRequestFailed(now);
          break;
        case wifi::RoamingPolicy::Action::QueryBtm:
          // An AP can supply its own candidate list. This handles a received
          // report, an empty report, and an unanswered neighbor request alike.
          if (esp_wnm_send_bss_transition_mgmt_query(REASON_FRAME_LOSS,
                                                    nullptr, 0) < 0)
            _roaming.transitionRequestFailed(now);
          break;
        case wifi::RoamingPolicy::Action::Scan:
          memcpy(_roamScanSsid, current.ssid, sizeof(_roamScanSsid));
          _roamScanConfig = {};
          _roamScanConfig.ssid = _roamScanSsid;
          _roamScanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
          _roamScanConfig.scan_time.active.min = 40;
          _roamScanConfig.scan_time.active.max = 80;
          xEventGroupClearBits(_eventGroup, WifiFlags::ScanDone);
          if (esp_wifi_scan_start(&_roamScanConfig, false) == ESP_OK) {
            _roamScan = true;
            _roamScanStarted = now;
            xEventGroupSetBits(_eventGroup, WifiFlags::Scanning);
          } else
            _roaming.scanComplete(now);
          break;
        case wifi::RoamingPolicy::Action::None:
          break;
      }
    }

    void Wifi::finishRoamingScan() {
      _roamScan = false;
      xEventGroupClearBits(_eventGroup,
                          WifiFlags::Scanning | WifiFlags::ScanDone);
      _roaming.scanComplete(millis());
      uint16_t count = 0;
      if (esp_wifi_scan_get_ap_num(&count) != ESP_OK || !count) {
        esp_wifi_clear_ap_list();
        return;
      }
      // Bound temporary RAM use even in a crowded environment. IDF returns
      // strongest records first and frees the remaining list on this call.
      count = std::min<uint16_t>(count, 32);
      auto* records = (wifi_ap_record_t*)calloc(count, sizeof(wifi_ap_record_t));
      if (!records) {
        esp_wifi_clear_ap_list();
        return;
      }
      const auto err = esp_wifi_scan_get_ap_records(&count, records);
      if (err != ESP_OK)
        esp_wifi_clear_ap_list();
      wifi_ap_record_t current = {}, best = {};
      wifi_config_t conf = {};
      bool found = false;
      if (err == ESP_OK && !_managedExternally && !_stopped && !_connect &&
          !_pendingResponse && !ota::isRunning() && _rssiThreshold &&
          isConnected() && esp_wifi_sta_get_ap_info(&current) == ESP_OK &&
          current.rssi < _rssiThreshold &&
          esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        for (uint16_t i = 0; i < count; ++i) {
          const auto& candidate = records[i];
          if (strcmp((const char*)candidate.ssid, (const char*)current.ssid) ||
              candidate.authmode < conf.sta.threshold.authmode)
            continue;
          // Active PSK credentials cannot authenticate an enterprise AP that
          // happens to advertise the same SSID.
          switch (candidate.authmode) {
            case WIFI_AUTH_WPA_PSK:
            case WIFI_AUTH_WPA2_PSK:
            case WIFI_AUTH_WPA_WPA2_PSK:
            case WIFI_AUTH_WPA3_PSK:
            case WIFI_AUTH_WPA2_WPA3_PSK:
              break;
            default:
              continue;
          }
          if (wifi::RoamingPolicy::betterCandidate(
                  current.rssi, candidate.rssi, _rssiThreshold,
                  !memcmp(current.bssid, candidate.bssid, 6)) &&
              (!found || candidate.rssi > best.rssi)) {
            best = candidate;
            found = true;
          }
        }
      }
      free(records);
      if (found) {
        logI("stronger AP " MACSTR " (%d -> %d dBm)", MAC2STR(best.bssid),
             (int)current.rssi, (int)best.rssi);
        roamTo(best);
      }
    }

    bool Wifi::roamTo(const wifi_ap_record_t& target) {
      if (_managedExternally || _stopped || _connect || ota::isRunning())
        return false;
      wifi_config_t previous = {};
      if (esp_wifi_get_config(WIFI_IF_STA, &previous) != ESP_OK ||
          strncmp((const char*)previous.sta.ssid, (const char*)target.ssid,
                  sizeof(previous.sta.ssid)))
        return false;
      // Use the credentials that actually authenticated this ESS. A saved
      // profile for a nearby BSSID may have an old password. This temporary
      // target must never be persisted as another user-pinned profile.
      char password[65] = {};
      memcpy(password, previous.sta.password, sizeof(previous.sta.password));
      ApInfo candidate(0, (const char*)target.ssid, password, target.bssid);
      bool connected = tryConnect(&candidate, false, false);
      if (_connect) {
        // A manual connection request arriving during the bounded attempt
        // must still be serviced even if the target just acquired an IP.
        _sta.disconnect();
        connected = false;
      }
      if (!connected) {
        esp_wifi_disconnect();
        waitState([this] { return !_sta.isConnected(); }, 1000);
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_wifi_set_config(WIFI_IF_STA, &previous));
        // Failed migration must return to ordinary bounded reconnection,
        // including the saved pinned preference and configured fallbacks.
        _roaming.disconnected(false, millis());
        _sta.setStatus(StaStatus::Initial);
      }
      return connected;
    }

    void Wifi::checkScan() {
      if (_roamScan)
        return;
      if (!_pendingResponse)
        return;
      if (xEventGroupGetBits(_eventGroup) & WifiFlags::ScanDone) {
        _scanStarted = 0;
        uint16_t sc = 0;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_get_ap_num(&sc));

        JsonDocument* doc = nullptr;
        if (sc > 0) {
          doc = new JsonDocument(/*
              JSON_ARRAY_SIZE(sc) +
              (sc * (JSON_ARRAY_SIZE(5) + 33 + net::MacMaxChars))*/);
          JsonArray arr = doc->to<JsonArray>();
          wifi_ap_record_t* scanResult =
              (wifi_ap_record_t*)calloc(sc, sizeof(wifi_ap_record_t));
          if (doc && scanResult &&
              ESP_ERROR_CHECK_WITHOUT_ABORT(
                  esp_wifi_scan_get_ap_records(&sc, scanResult)) == ESP_OK) {
            for (uint16_t i = 0; i < sc; i++) {
              wifi_ap_record_t* r = scanResult + i;
              auto entry = arr.add<JsonArray>();
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
          _rssi.set(info.rssi);
        }
      }
      return true;
    };

    Wifi* useWifi() {
      return &Wifi::instance();
    }

    namespace wifi {
      void addAccessPoint(const char* ssid, const char* password,
                          const uint8_t* bssid) {
        Wifi::instance().addFallback(ssid, password, bssid);
      }
    }  // namespace wifi
  }  // namespace net
}  // namespace esp32m
