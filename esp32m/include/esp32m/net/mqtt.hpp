#pragma once

#include "esp32m/app.hpp"
#include "esp32m/sleep.hpp"

#include <mqtt_client.h>
#include <mutex>
#include <string>

namespace esp32m {
  namespace net {

    class Mqtt;

    namespace mqtt {

      enum class Status {
        Initial,
        Connecting,
        Connected,
        Subscribing,
        Ready,
        Disconnecting,
        Disconnected
      };

      class StatusChanged : public Event {
       public:
        Status next() const {
          return _next;
        }
        Status prev() const {
          return _prev;
        }
        static bool is(Event &ev) {
          return ev.is(NAME);
        }

       private:
        StatusChanged(Status next, Status prev)
            : Event(NAME), _next(next), _prev(prev) {}
        Status _next, _prev;
        static const char *NAME;
        friend class net::Mqtt;
      };

      class Incoming : public Event {
       public:
        std::string topic() const {
          return _topic;
        }
        std::string payload() const {
          return _payload;
        }
        static bool is(Event &ev, const char *topic = nullptr) {
          return ev.is(NAME) && (!topic || ((Incoming &)ev)._topic == topic);
        }

       private:
        Incoming(std::string topic, std::string payload)
            : Event(NAME), _topic(topic), _payload(payload) {}
        std::string _topic, _payload;
        static const char *NAME;
        friend class net::Mqtt;
      };

      typedef std::function<void(std::string topic, std::string payload)>
          HandlerFunction;

      class Subscription {
       public:
        ~Subscription();
        int id() const {
          return _id;
        }
        int qos() const {
          return _qos;
        }
        std::string topic() const {
          return _topic;
        }

       private:
        Subscription(Mqtt *mqtt, int id, std::string topic, int qos,
                     HandlerFunction function)
            : _mqtt(mqtt),
              _id(id),
              _qos(qos),
              _topic(topic),
              _function(function){};
        Subscription(const Subscription &) = delete;
        Mqtt *_mqtt;
        int _id, _qos;
        std::string _topic;
        HandlerFunction _function;
        friend class net::Mqtt;
      };

    }  // namespace mqtt

    using namespace mqtt;

    class Mqtt : public AppObject {
     public:
      Mqtt(const Mqtt &) = delete;
      static Mqtt &instance();

      const char *name() const override {
        return "mqtt";
      }
      bool isReady();
      bool isConnected();
      bool publish(const char *topic, const char *message, int qos = 0,
                   bool retain = false);
      bool enqueue(const char *topic, const char *message, int qos = 0,
                   bool retain = false, bool store = false);
      Subscription *subscribe(const char *topic, int qos = 0);
      Subscription *subscribe(const char *topic, HandlerFunction handler,
                              int qos = 0);

     protected:
      bool handleEvent(Event &ev) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(RequestContext &ctx) override;

     private:
      Mqtt();
      TaskHandle_t _task = nullptr;
      esp_mqtt_client_handle_t _handle = nullptr;
      esp_mqtt_client_config_t _cfg;
      Status _status = Status::Initial;
      std::mutex _mutex;
      std::map<std::string, std::map<int, Subscription *> > _subscriptions;
      void setState(Status state);

      bool _enabled = true, _configChanged = false, _listen = true;
      std::string _uri, _username, _password, _client;
      char *_commandTopic = nullptr;
      char *_sensorsTopic = nullptr;
      char *_responseTopic = nullptr;
      char *_broadcastTopic = nullptr;
      uint32_t _cmdcnt = 0, _pubcnt = 0;
      unsigned long _timer = 0;
      int _timeout = 30;
      esp_err_t handle(int32_t event_id, void *event_data);
      void run();
      void disconnect();
      void respond(const char *source, int seq, const JsonVariantConst data,
                   bool isError);
      bool intSubscribe(std::string topic, int qos = 0);
      void unsubscribe(Subscription *sub);
      void prepareCfg();
      const char* effectiveClient();
      friend class MqttRequest;
      friend class mqtt::Subscription;
    };

    Mqtt *useMqtt();

  }  // namespace net
}  // namespace esp32m
