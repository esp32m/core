#include <esp_task_wdt.h>

#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/device.hpp"
#include "esp32m/events.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/json.hpp"
#include "esp32m/net/ip_event.hpp"
#include "esp32m/net/mqtt.hpp"
#include "esp32m/net/wifi.hpp"

namespace esp32m {
  namespace net {

    class MqttRequest : public Request {
     public:
      MqttRequest(const char *name, int id, const char *target,
                  const JsonVariantConst data)
          : Request(name, id, target, data) {}

     protected:
      void respondImpl(const char *source, const JsonVariantConst data,
                       bool isError) override {
        Mqtt::instance().respond(source, seq(), data, isError);
      }
      Response *makeResponseImpl() override {
        return new Response(Mqtt::instance().name(), name(), target(), seq());
      }
    };

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

    void setCfgStr(const char **dest, const char *src, bool &changed) {
      if ((*dest == nullptr && src == nullptr) ||
          (*dest != nullptr && src != nullptr && !strcmp(*dest, src)))
        return;
      changed = true;
      if (*dest)
        free((void *)*dest);
      *dest = nullptr;
      auto l = src ? strlen(src) : 0;
      if (l) {
        auto m = (char *)malloc(l + 1);
        if (m) {
          *dest = m;
          memcpy(m, src, l + 1);
        }
      }
    }

    Mqtt::Mqtt() {
      memset(&_cfg, 0, sizeof(esp_mqtt_client_config_t));
      bool changed;
      // don't assign directly - _cfg.uri may bee free()'d
      setCfgStr(&_cfg.broker.address.uri, "mqtt://mqtt.lan", changed);
      _cfg.session.keepalive = 120;
    }

    void Mqtt::setState(State state) {
      std::lock_guard guard(_mutex);
      if (_state == state)
        return;
      /*if (state == State::Ready)
        _sleepBlocker.unblock();
      if (_state == State::Ready)
        _sleepBlocker.block();*/
      _state = state;
      xTaskNotifyGive(_task);
    }

    bool Mqtt::handleEvent(Event &ev) {
      JsonObject cr;
      JsonObjectConst ca;
      DoneReason reason;
      sleep::Event *slev;
      if (EventInit::is(ev, 0)) {
        xTaskCreate([](void *self) { ((Mqtt *)self)->run(); }, "m/mqtt", 4096,
                    this, tskIDLE_PRIORITY, &_task);
        _handle = esp_mqtt_client_init(&_cfg);
        esp_mqtt_client_register_event(
            _handle, MQTT_EVENT_ANY,
            [](void *handler_args, esp_event_base_t base, int32_t event_id,
               void *event_data) {
              ((Mqtt *)handler_args)->handle(event_id, event_data);
            },
            (void *)this);
        auto name = App::instance().name();
        if (asprintf(&_commandTopic, "esp32m/request/%s/#", name) < 0)
          _commandTopic = nullptr;
        if (asprintf(&_sensorsTopic, "esp32m/sensor/%s", name) < 0)
          _sensorsTopic = nullptr;
        if (asprintf(&_responseTopic, "esp32m/response/%s/", name) < 0)
          _responseTopic = nullptr;
        if (asprintf(&_broadcastTopic, "esp32m/broadcast/%s/", name) < 0)
          _responseTopic = nullptr;
      } else if (EventDone::is(ev, &reason)) {
        if (reason != DoneReason::LightSleep) {
          _enabled = false;
          disconnect();
          logD("stopping...");
          waitState([this] { return _state == State::Initial; }, 500);
          logD("stopped");
        }
      } else if (IpEvent::is(ev, IP_EVENT_STA_GOT_IP, nullptr))
        xTaskNotifyGive(_task);
      else if (sleep::Event::is(ev, &slev)) {
        if (_state != State::Ready)
          slev->block();
      } else if (isConnected()) {
        Broadcast *b = nullptr;
        Response *r = nullptr;
        if (EventSensor::is(ev)) {
          auto e = ((EventSensor &)ev);
          char *devProps, *props;
          auto unitName = App::instance().name();
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
        } else if (Response::is(ev, name(), &r)) {
          DynamicJsonDocument *doc = r->data();
          JsonVariantConst data = doc ? doc->as<JsonVariantConst>()
                                      : json::null<JsonVariantConst>();
          respond(r->source(), r->seq(), data, r->isError());
        } else
          return false;
      } else
        return false;
      return true;
    }

    void Mqtt::run() {
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        if (!_enabled) {
          if (_state != State::Initial)
            disconnect();
          continue;
        }
        if (_configChanged)
          disconnect();
        _configChanged = false;
        switch (_state) {
          case State::Initial:
            _timer = 0;
            if (Wifi::instance().isConnected() && _cfg.broker.address.uri &&
                strlen(_cfg.broker.address.uri)) {
              setState(State::Connecting);
              logI("connecting to %s", _cfg.broker.address.uri);
              ESP_ERROR_CHECK_WITHOUT_ABORT(
                  esp_mqtt_client_set_uri(_handle, _cfg.broker.address.uri));
              ESP_ERROR_CHECK_WITHOUT_ABORT(
                  esp_mqtt_set_config(_handle, &_cfg));
              ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_start(_handle));
              _timer = millis();
            }
            break;
          case State::Connecting:
            if (millis() - _timer > _timeout * 1000) {
              logW("timeout connecting to %s", _cfg.broker.address.uri);
              setState(State::Initial);
            }
            break;
          case State::Connected:
            if (_listen) {
              setState(State::Subscribing);
              logI("connected, subscribing to %s : %d", _commandTopic,
                   esp_mqtt_client_subscribe(_handle, _commandTopic, 0));
              _timer = millis();
            } else {
              setState(State::Ready);
              _timer = 0;
              logI("connected");
            }
            break;
          case State::Subscribing:
            if (millis() - _timer > _timeout * 1000) {
              logW("timeout subscribing to %s", _commandTopic);
              setState(State::Connected);
            }
            break;
          case State::Disconnecting:
            if (millis() - _timer > _timeout * 1000) {
              logW("timeout disconnecting");
              setState(State::Initial);
            }
            break;
          case State::Disconnected:
            logI("disconnected");
            setState(State::Initial);
            break;
          default:
            break;
        }
      }
    }

    DynamicJsonDocument *Mqtt::getState(const JsonVariantConst args) {
      size_t size = JSON_OBJECT_SIZE(5);
      auto doc = new DynamicJsonDocument(size);
      auto cr = doc->to<JsonObject>();
      bool ready = _state == State::Ready;
      cr["ready"] = ready;
      if (ready) {
        if (_cfg.broker.address.uri)
          cr["uri"] = _cfg.broker.address.uri;
        if (_cfg.credentials.client_id)
          cr["client"] = _cfg.credentials.client_id;
      }
      cr["pubcnt"] = _pubcnt;
      cr["cmdcnt"] = _cmdcnt;
      return doc;
    }

    DynamicJsonDocument *Mqtt::getConfig(const JsonVariantConst args) {
      size_t size = JSON_OBJECT_SIZE(1 + 8) +
                    (64 * 4);  // mqtt: enabled, control, uri, username,
                               // password, client, keepalive, timeout

      auto doc = new DynamicJsonDocument(size);
      auto cr = doc->to<JsonObject>();
      if (_enabled)
        cr["enabled"] = _enabled;
      if (_cfg.broker.address.uri && strlen(_cfg.broker.address.uri))
        cr["uri"] = (char *)_cfg.broker.address.uri;
      if (_cfg.credentials.username && strlen(_cfg.credentials.username))
        cr["username"] = (char *)_cfg.credentials.username;
      if (_cfg.credentials.authentication.password && strlen(_cfg.credentials.authentication.password))
        cr["password"] = (char *)_cfg.credentials.authentication.password;
      if (_cfg.credentials.client_id && strlen(_cfg.credentials.client_id) &&
          strcmp(_cfg.credentials.client_id, App::instance().name()))
        cr["client"] = (char *)_cfg.credentials.client_id;
      cr["keepalive"] = _cfg.session.keepalive;
      cr["timeout"] = _timeout;
      return doc;
    }

    bool Mqtt::setConfig(const JsonVariantConst ca,
                         DynamicJsonDocument **result) {
      bool changed = false;
      json::from(ca["enabled"], _enabled, &changed);
      // _sleepBlocker.enable(_enabled);
      setCfgStr(&_cfg.broker.address.uri, ca["uri"].as<const char *>(),
                changed);
      setCfgStr(&_cfg.credentials.username, ca["username"].as<const char *>(),
                changed);
      setCfgStr(&_cfg.credentials.authentication.password,
                ca["password"].as<const char *>(), changed);
      const char *client = ca["client"].as<const char *>();
      if (!client)
        client = App::instance().name();
      setCfgStr(&_cfg.credentials.client_id, client, changed);
      json::from(ca["keepalive"], _cfg.session.keepalive, &changed);
      json::from(ca["timeout"], _timeout, &changed);
      if (_timeout < 1)
        _timeout = 1;
      _configChanged = changed;
      if (_task)
        xTaskNotifyGive(_task);
      return changed;
    }

    bool Mqtt::publish(const char *topic, const char *message) {
      if (!isConnected() || !topic || !message)
        return false;
      // logD("publish %s %s", topic, message);
      bool result =
          ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_publish(
              _handle, topic, message, strlen(message), 0, false)) == ESP_OK;
      if (result)
        _pubcnt++;
      return result;
    }

    void Mqtt::respond(const char *source, int seq, const JsonVariantConst data,
                       bool isError) {
      size_t tl = strlen(_responseTopic) + strlen(source) + 1;
      if (seq)
        tl += 1 + 10;
      if (isError)
        tl += 6;
      char *topic = (char *)malloc(tl);
      char *tp = topic;
      size_t l = snprintf(tp, tl, "%s%s", _responseTopic, source);
      tp += l;
      tl -= l;
      if (seq && tl) {
        l = snprintf(tp, tl, "/%d", seq);
        tp += l;
        tl -= l;
      }
      if (isError && tl)
        strlcpy(tp, "/error", tl);
      char *ds = json::allocSerialize(data);
      publish(topic, ds);
      free(topic);
      if (ds)
        free(ds);
    }

    esp_err_t Mqtt::handle(int32_t event_id, void *event_data) {
      esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
      size_t ctl;
      switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_ERROR:
          logE("error %i", event->error_handle->error_type);
          break;
        case MQTT_EVENT_CONNECTED:
          setState(State::Connected);
          _timer = 0;
          break;
        case MQTT_EVENT_DISCONNECTED:
          setState(State::Disconnected);
          _timer = 0;
          break;
        case MQTT_EVENT_SUBSCRIBED:
          setState(State::Ready);
          _timer = 0;
          break;
        case MQTT_EVENT_UNSUBSCRIBED:
          setState(State::Connected);
          _timer = 0;
          break;
        case MQTT_EVENT_DATA:
          if (!_commandTopic)
            break;
          ctl = strlen(_commandTopic);
          if ((event->topic_len >= ctl) &&
              !strncmp(event->topic, _commandTopic, ctl - 1)) {
            auto devlen = event->topic_len - ctl + 1 /* / */ + 1 /* # */;
            auto cmdStart = event->topic + ctl - 1 /* # */;
            auto firstSlash = (char *)memchr(cmdStart, '/', devlen - 1);
            if (!firstSlash)
              break;

            char *devname = strndup(cmdStart, firstSlash - cmdStart);
            char *command = strndup(firstSlash + 1,
                                    devlen - 1 - (firstSlash - cmdStart + 1));
            DynamicJsonDocument *doc = nullptr;
            char *data = nullptr;
            if (event->data_len) {
              data = (char *)malloc(event->data_len + 1);
              strlcpy(data, event->data, event->data_len + 1);
              doc = json::parse(data);
            }
            _cmdcnt++;
            MqttRequest req(command, 0, devname,
                            doc ? doc->as<JsonVariantConst>()
                                : json::null<JsonVariantConst>());
            req.publish();
            free(devname);
            free(command);
            if (data)
              free(data);
            if (doc)
              free(doc);
          }
          break;
        default:
          break;
      }
      return 0;
    }

    void Mqtt::disconnect() {
      if (_state == State::Disconnecting || _state == State::Initial)
        return;
      setState(State::Disconnecting);
      _timer = millis();
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_stop(_handle));
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