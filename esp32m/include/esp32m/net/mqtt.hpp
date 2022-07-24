#pragma once

#include "esp32m/app.hpp"
#include "esp32m/sleep.hpp"

#include <mqtt_client.h>
#include <mutex>

namespace esp32m {
  namespace net {
    class Mqtt : public AppObject {
     public:
      Mqtt(const Mqtt &) = delete;
      static Mqtt &instance();

      const char *name() const override {
        return "mqtt";
      }
      bool isReady() {
        return _state == State::Ready;
      }
      bool isConnected() {
        return _state == State::Ready || _state == State::Connected ||
               _state == State::Subscribing;
      }
      bool publish(const char *topic, const char *message);
      void setListen(bool on) {
        _listen = on;
      }
      bool getListen() const {
        return _listen;
      }

     protected:
      bool handleEvent(Event &ev) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;

     private:
      enum class State {
        Initial,
        Connecting,
        Connected,
        Subscribing,
        Ready,
        Disconnecting,
        Disconnected
      };
      Mqtt();
      TaskHandle_t _task = nullptr;
      esp_mqtt_client_handle_t _handle = nullptr;
      esp_mqtt_client_config_t _cfg;
      State _state = State::Initial;
      std::mutex _mutex;
      // sleep::Blocker _sleepBlocker;
      void setState(State state);

      bool _enabled = true, _configChanged = false, _listen = true;
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
      friend class MqttRequest;
    };

    Mqtt *useMqtt();

  }  // namespace net
}  // namespace esp32m
