#include <esp_task_wdt.h>

#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/device.hpp"
#include "esp32m/events.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/json.hpp"
#include "esp32m/net/http.hpp"
#include "esp32m/net/ip_event.hpp"
#include "esp32m/net/mqtt.hpp"
#include "esp32m/net/net.hpp"

namespace esp32m {
  namespace net {

    namespace mqtt {
      const char *StatusChanged::NAME = "mqtt-status";
      const char *Incoming::NAME = "mqtt-incoming";
      int subscriptionIdCounter = 0;
      Subscription::~Subscription() {
        _mqtt->unsubscribe(this);
      }
    }  // namespace mqtt

    using namespace mqtt;

    size_t serializeProps(const JsonObjectConst props, char **buf) {
      if (props) {
        auto size = measureJson(props);
        char *bptr = *buf = (char *)malloc(size);

        for (JsonPairConst kv : props) {
          if (!size)
            break;
          if (bptr != *buf) {
            *(++bptr) = ',';
            size--;
          }
          auto l = snprintf(bptr, size, "%s=", kv.key().c_str());
          bptr += l;
          size -= l;
          if (!size)
            break;
          if (kv.value().is<const char *>())
            l = strlcpy(bptr, kv.value().as<const char *>(), size);
          else
            l = serializeJson(kv.value(), bptr, size);
          bptr += l;
          size -= l;
        }
        return bptr - *buf;
      }

      return 0;
    }

    Mqtt::Mqtt() : _certCache("/root/mqtt-cert", http::Client::instance()) {
      memset(&_cfg, 0, sizeof(esp_mqtt_client_config_t));
      _uri = "mqtt://mqtt.lan";
      _cfg.session.keepalive = 120;
    }

    bool Mqtt::isReady() {
      return _handle && _status == Status::Ready;
    }

    bool Mqtt::isConnected() {
      return _handle &&
             (_status == Status::Ready || _status == Status::Connected);
    }

    void Mqtt::setState(Status status) {
      Status prev;
      {
        std::lock_guard guard(_mutex);
        if (_status == status)
          return;
        prev = _status;
        _status = status;
      }
      mqtt::StatusChanged ev(status, prev);
      ev.publish();
      xTaskNotifyGive(_task);
    }

    bool Mqtt::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("clear-cert-cache")) {
        _certCache.clear();
        _cert.reset();
        req.respond();
        return true;
      }
      return false;
    }

    void Mqtt::handleEvent(Event &ev) {
      JsonObject cr;
      JsonObjectConst ca;
      DoneReason reason;
      sleep::Event *slev;
      if (EventInit::is(ev, 0)) {
        xTaskCreate([](void *self) { ((Mqtt *)self)->run(); }, "m/mqtt", 4096,
                    this, tskIDLE_PRIORITY, &_task);
        prepareCfg(true);
        _handle = esp_mqtt_client_init(&_cfg);
        esp_mqtt_client_register_event(
            _handle, MQTT_EVENT_ANY,
            [](void *handler_args, esp_event_base_t base, int32_t event_id,
               void *event_data) {
              ((Mqtt *)handler_args)->handle(event_id, event_data);
            },
            (void *)this);
        auto name = App::instance().hostname();
        if (asprintf(&_sensorsTopic, "esp32m/sensor/%s", name) < 0)
          _sensorsTopic = nullptr;
        if (asprintf(&_broadcastTopic, "esp32m/broadcast/%s/", name) < 0)
          _broadcastTopic = nullptr;
      } else if (EventDone::is(ev, &reason)) {
        if (reason != DoneReason::LightSleep) {
          _enabled = false;
          disconnect();
          logD("stopping...");
          waitState([this] { return _status == Status::Initial; }, 500);
          logD("stopped");
        }
      } else if (IpEvent::is(ev, IP_EVENT_STA_GOT_IP, nullptr))
        xTaskNotifyGive(_task);
      else if (sleep::Event::is(ev, &slev)) {
        if (_status != Status::Ready)
          slev->block();
      } else if (isConnected()) {
        Broadcast *b = nullptr;
        if (EventSensor::is(ev)) {
          auto e = ((EventSensor &)ev);
          char *devProps, *props;
          auto unitName = App::instance().hostname();
          auto devName = e.device().name();
          auto dpl = serializeProps(e.device().props(), &devProps);
          auto pl = serializeProps(e.props(), &props);
          auto sensor = e.sensor();
          auto unl = strlen(unitName);
          auto dnl = strlen(devName);
          auto dl = 12 /* "esp32m,unit=" */ + unl + 8 /* ",device=" */ + dnl +
                    (dpl ? (1 /* comma */ + dpl) : 0) +
                    (pl ? (1 /* comma */ + pl) : 0) + 1 /*space*/ +
                    strlen(sensor) + 1 /*=*/ + (16 + 1 /*value*/) + 1 /*null*/;
          char *s = (char *)malloc(dl);
          char *sp = s;
          auto l =
              snprintf(sp, dl, "esp32m,unit=%s,device=%s", unitName, devName);
          sp += l;
          dl -= l;
          if (dpl && dl) {
            l = snprintf(sp, dl, ",%s", devProps);
            sp += l;
            dl -= l;
          }
          if (pl && dl) {
            l = snprintf(sp, dl, ",%s", props);
            sp += l;
            dl -= l;
          }
          if (dl) {
            l = snprintf(sp, dl, " %s=%.16g", sensor, e.value());
            sp += l;
            dl -= l;
          }
          if (dpl)
            free(devProps);
          if (pl)
            free(props);
          publish(_sensorsTopic, s);
          free(s);
        } else if (Broadcast::is(ev, &b)) {
          char *ds = json::allocSerialize(b->data());
          size_t tl = strlen(_broadcastTopic) + strlen(b->source()) + 1 +
                      strlen(b->name()) + 1;
          char *topic = (char *)malloc(tl);
          snprintf(topic, tl, "%s%s/%s", _broadcastTopic, b->source(),
                   b->name());
          publish(topic, ds);
          free(topic);
          if (ds)
            free(ds);
        }
      }
    }

    void Mqtt::run() {
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        if (!_enabled) {
          if (_status != Status::Initial)
            disconnect();
          continue;
        }
        if (_configChanged)
          disconnect();
        _configChanged = false;
        switch (_status) {
          case Status::Initial:
            _timer = 0;
            if (net::isDnsResponding() && !_uri.empty()) {
              setState(Status::Connecting);
              logI("connecting to %s", _uri.c_str());
              /*ESP_ERROR_CHECK_WITHOUT_ABORT(
                  esp_mqtt_client_set_uri(_handle, _cfg.broker.address.uri));*/
              prepareCfg(false);
              ESP_ERROR_CHECK_WITHOUT_ABORT(
                  esp_mqtt_set_config(_handle, &_cfg));
              ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_start(_handle));
              _timer = millis();
            }
            break;
          case Status::Connecting:
            if (millis() - _timer > _timeout * 1000) {
              logW("timeout connecting to %s", _uri.c_str());
              setState(Status::Initial);
            }
            break;
          case Status::Connected: {
            logI("connected");
            std::map<std::string, int> toSubscribe;
            {
              std::lock_guard guard(_mutex);
              for (auto const &[topic, map] : _subscriptions) {
                int qos = 0;
                for (auto const &[id, sub] : map)
                  if (sub->qos() > qos)
                    qos = sub->qos();
                toSubscribe[topic] = qos;
              }
            }
            for (auto const &[topic, qos] : toSubscribe)
              this->intSubscribe(topic, qos);
            setState(Status::Ready);
            _timer = 0;
          } break;
          case Status::Disconnecting:
            break;
          case Status::Disconnected:
            logI("disconnected");
            setState(Status::Initial);
            break;
          default:
            break;
        }
      }
    }

    const char *Mqtt::effectiveClient() {
      if (_client.empty())
        return App::instance().hostname();
      return _client.c_str();
    }

    DynamicJsonDocument *Mqtt::getState(const JsonVariantConst args) {
      char *client = (char *)effectiveClient();
      size_t size = JSON_OBJECT_SIZE(5 + 1) + JSON_STRING_SIZE(_uri.size()) +
                    JSON_STRING_SIZE(strlen(client));
      auto doc = new DynamicJsonDocument(size);
      auto cr = doc->to<JsonObject>();
      bool ready = _status == Status::Ready;
      cr["ready"] = ready;
      if (ready) {
        json::to(cr, "uri", _uri);
        json::to(cr, "client", client);
      }
      cr["pubcnt"] = _pubcnt;
      cr["recvcnt"] = _recvcnt;
      return doc;
    }

    DynamicJsonDocument *Mqtt::getConfig(RequestContext &ctx) {
      size_t size =
          JSON_OBJECT_SIZE(1 + 8) +  // mqtt: enabled, uri, username, password,
                                     // client, cert_url, keepalive, timeout
          JSON_STRING_SIZE(_uri.size()) + JSON_STRING_SIZE(_username.size()) +
          JSON_STRING_SIZE(_password.size()) +
          JSON_STRING_SIZE(_client.size()) + JSON_STRING_SIZE(_certurl.size());

      auto doc = new DynamicJsonDocument(size);
      auto cr = doc->to<JsonObject>();
      cr["enabled"] = _enabled;
      json::to(cr, "uri", _uri);
      json::to(cr, "username", _username);
      json::to(cr, "password", _password);
      if (_client != App::instance().hostname())
        json::to(cr, "client", _client);
      json::to(cr, "cert_url", _certurl);
      cr["keepalive"] = _cfg.session.keepalive;
      cr["timeout"] = _timeout;
      return doc;
    }

    void Mqtt::prepareCfg(bool init) {
      _cfg.broker.address.uri = _uri.c_str();
      _cfg.credentials.username = _username.c_str();
      _cfg.credentials.authentication.password = _password.c_str();
      _cfg.credentials.client_id = effectiveClient();
      _cfg.broker.verification.certificate = nullptr;
      if (!_certurl.empty() && !init) {
        if (!_cert || _cert->size() == 0) {
          UrlResourceRequest req(_certurl.c_str());
          req.options.addNullTerminator = true;
          _cert.reset(_certCache.obtain(req));
          /*          auto &errors = req.errors();
                    if (!errors.empty())
                      errors.dump();*/
        }
        if (_cert) {
          _cert->getData((void **)&_cfg.broker.verification.certificate);
          // _cfg.broker.verification.skip_cert_common_name_check = true;
          // printf("got cert size %d", size);
          // printf("%s", (const char *)_cfg.broker.verification.certificate);
        }
      }
    }

    bool Mqtt::setConfig(const JsonVariantConst ca,
                         DynamicJsonDocument **result) {
      bool changed = false;
      json::from(ca["enabled"], _enabled, &changed);
      json::from(ca["uri"], _uri, &changed);
      json::from(ca["username"], _username, &changed);
      json::from(ca["password"], _password, &changed);
      json::from(ca["client"], _client, &changed);
      json::from(ca["cert_url"], _certurl, &changed);
      json::from(ca["keepalive"], _cfg.session.keepalive, &changed);
      json::from(ca["timeout"], _timeout, &changed);
      if (_timeout < 1)
        _timeout = 1;
      _configChanged = changed;
      if (_task)
        xTaskNotifyGive(_task);
      return changed;
    }

    bool Mqtt::publish(const char *topic, const char *message, int qos,
                       bool retain) {
      if (!_handle || !isConnected() || !topic || !message)
        return false;
      // logD("publish %s %s", topic, message);
      auto id = esp_mqtt_client_publish(_handle, topic, message,
                                        strlen(message), qos, retain);
      if (id >= 0)
        _pubcnt++;
      return id >= 0;
    }
    bool Mqtt::enqueue(const char *topic, const char *message, int qos,
                       bool retain, bool store) {
      if (!_handle || !isConnected() || !topic || !message)
        return false;
      // logD("enqueue %s %s", topic, message);
      auto id = esp_mqtt_client_enqueue(_handle, topic, message,
                                        strlen(message), qos, retain, store);
      if (id >= 0)
        _pubcnt++;
      return id >= 0;
    }

    void Mqtt::setLwt(const char *topic, const char *message, int qos,
                      bool retain) {
      if (topic && strlen(topic))
        _lwtTopic = topic;
      else
        _lwtTopic.clear();
      if (message && strlen(message))
        _lwtMessage = message;
      else
        _lwtMessage.clear();
      _cfg.session.last_will = {
          .topic = _lwtTopic.empty() ? nullptr : _lwtTopic.c_str(),
          .msg = _lwtMessage.empty() ? nullptr : _lwtMessage.c_str(),
          .msg_len = (int)_lwtMessage.size(),
          .qos = qos,
          .retain = retain};
      _configChanged = true;
    }  // namespace net

    bool Mqtt::intSubscribe(std::string topic, int qos) {
      auto id = esp_mqtt_client_subscribe(_handle, topic.c_str(), qos);
      if (id >= 0) {
        logI("subscribed to %s (qos=%d): %d", topic.c_str(), qos, id);
        return true;
      }
      logW("failed to subscribe to %s (qos=%d)", topic.c_str(), qos);
      return false;
    }

    Subscription *Mqtt::subscribe(const char *topic, int qos) {
      return this->subscribe(topic, nullptr, qos);
    }
    Subscription *Mqtt::subscribe(const char *topic, HandlerFunction handler,
                                  int qos) {
      if (!topic)
        return nullptr;
      std::string t(topic);
      auto id = ++subscriptionIdCounter;
      auto sub = new Subscription(this, id, t, qos, handler);
      bool sendSubscribe;
      {
        std::lock_guard guard(_mutex);
        auto &subs = _subscriptions[t];
        sendSubscribe = subs.size() == 0 && isConnected();
        subs[id] = sub;
      }
      if (sendSubscribe)
        intSubscribe(t, qos);
      return sub;
    }

    void Mqtt::unsubscribe(Subscription *sub) {
      if (!sub)
        return;
      bool sendUnsubscribe = false;
      {
        std::lock_guard guard(_mutex);
        auto subsIt = _subscriptions.find(sub->topic());
        if (subsIt == _subscriptions.end())
          return;
        auto subsByid = subsIt->second;
        auto subIt = subsByid.find(sub->id());
        if (subIt == subsByid.end())
          return;
        subsByid.erase(subIt);
        if (subsByid.size() == 0) {
          _subscriptions.erase(subsIt);
          sendUnsubscribe = isConnected();
        }
      }
      if (sendUnsubscribe)
        esp_mqtt_client_unsubscribe(_handle, sub->topic().c_str());
    }

    esp_err_t Mqtt::handle(int32_t event_id, void *event_data) {
      esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
      switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_ERROR:
          logE("error %i", event->error_handle->error_type);
          break;
        case MQTT_EVENT_CONNECTED:
          setState(Status::Connected);
          _timer = 0;
          break;
        case MQTT_EVENT_DISCONNECTED:
          setState(Status::Disconnected);
          break;
        case MQTT_EVENT_SUBSCRIBED:
          break;
        case MQTT_EVENT_UNSUBSCRIBED:
          break;
        case MQTT_EVENT_DATA: {
          std::string topic = std::string(event->topic, event->topic_len);
          std::string payload = std::string(event->data, event->data_len);
          _recvcnt++;
          Incoming ev(topic, payload);
          ev.publish();
          std::vector<HandlerFunction> handlers;
          {
            std::lock_guard guard(_mutex);
            auto it = _subscriptions.find(topic);
            if (it != _subscriptions.end())
              for (auto const &[id, sub] : it->second) {
                auto fn = sub->_function;
                if (fn)
                  handlers.push_back(fn);
              }
          }
          for (auto fn : handlers) fn(topic, payload);
        } break;
        default:
          break;
      }
      return 0;
    }

    void Mqtt::disconnect() {
      if (_status == Status::Disconnecting || _status == Status::Initial ||
          _status == Status::Disconnected)
        return;
      setState(Status::Disconnecting);
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_stop(_handle));
      setState(Status::Disconnected);  // we only get MQTT_EVENT_DISCONNECTED on
                                       // network error or server disconnect
    }

    Mqtt &Mqtt::instance() {
      static Mqtt i;
      return i;
    }

    Mqtt *useMqtt() {
      return &Mqtt::instance();
    }
  }  // namespace net
}  // namespace esp32m