#pragma once

#include "esp32m/app.hpp"
#include "esp32m/fs/cache.hpp"
#include "esp32m/resources.hpp"
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
        static bool is(Event &ev, Status next) {
          return ev.is(NAME) && ((StatusChanged &)ev).next() == next;
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

      struct Message {
        std::string topic;
        std::string payload;
        int qos = 0;
        bool retain = false;

        bool valid() const {
          return !topic.empty() && !payload.empty() && qos >= 0;
        }

       private:
        void set(const char *topic, const char *message, int qos, bool retain) {
          if (topic && strlen(topic))
            this->topic = topic;
          else
            this->topic.clear();
          if (message && strlen(message))
            this->payload = message;
          else
            this->payload.clear();
          this->qos = qos;
          this->retain = retain;
        }
        friend class net::Mqtt;
      };

      class StatePublisher : public AppObject {
       public:
        StatePublisher(const StatePublisher &) = delete;
        const char *name() const override {
          return "mqtt-sp";
        }

        static StatePublisher &instance() {
          static StatePublisher i;
          return i;
        }

       protected:
        void handleEvent(Event &ev) override;

       private:
        json::DynamicObject _buffer;
        StatePublisher() {}
        esp_err_t publish(const char *name, JsonVariantConst state,
                          bool fromBuffer);
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
      void setLwt(const char *topic, const char *message, int qos = 1,
                  bool retain = true);
      const Message &getLwt() const {
        return _lwt;
      }
      /**
       * If no birth message is set, the default
       * ["esp32m/${hostname}/availability", "online"] will be used.
       * To disable default birth message, call setBirth with qos=-1
       */
      void setBirth(const char *topic, const char *message, int qos = 1,
                    bool retain = true);
      const Message &getBirth() const {
        return _birth;
      }

     protected:
      void handleEvent(Event &ev) override;
      bool handleRequest(Request &req) override;
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

      bool _enabled = true, _configChanged = false;
      std::string _uri, _username, _password, _client, _certurl;
      mqtt::Message _birth;
      mqtt::Message _lwt;
      std::unique_ptr<Resource> _cert;
      fs::CachedResource _certCache;
      char *_sensorsTopic = nullptr;
      char *_broadcastTopic = nullptr;
      uint32_t _pubcnt = 0, _recvcnt = 0;
      unsigned long _timer = 0;
      int _timeout = 30;
      esp_err_t handle(int32_t event_id, void *event_data);
      void run();
      void disconnect();
      bool intSubscribe(std::string topic, int qos = 0);
      void unsubscribe(Subscription *sub);
      void prepareCfg(bool init);
      void publishBirth();
      const char *effectiveClient();
      friend class mqtt::Subscription;
    };

    Mqtt *useMqtt();

  }  // namespace net
}  // namespace esp32m
