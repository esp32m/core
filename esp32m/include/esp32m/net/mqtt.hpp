#pragma once

#include <mqtt_client.h>

#include "esp32m/app.hpp"

namespace esp32m {
  namespace net {
    class Mqtt : public AppObject {
     public:
      Mqtt(const Mqtt &) = delete;
      static Mqtt &instance();

      const char *name() const override {
        return "mqtt";
      };
      bool isReady() {
        return _state == State::Ready;
      }
      bool publish(const char *topic, const char *message);

     protected:
      bool handleEvent(Event &ev) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;

     private:
      enum State {
        Initial,
        Connecting,
        Connected,
        Subscribing,
        Ready,
        Disconnecting,
      };
      Mqtt();
      TaskHandle_t _task = nullptr;
      esp_mqtt_client_handle_t _handle = nullptr;
      esp_mqtt_client_config_t _cfg;
      State _state = State::Initial;
      bool _enabled = true, _configChanged = false;
      char *_commandTopic = nullptr;
      char *_sensorsTopic = nullptr;
      char *_responseTopic = nullptr;
      char *_broadcastTopic = nullptr;
      uint32_t _cmdcnt = 0, _pubcnt = 0;
      unsigned long _timer = 0;
      int _timeout = 30;
      esp_err_t handle(esp_mqtt_event_handle_t event);
      void run();
      void disconnect();
      void respond(const char *source, int seq, const JsonVariantConst data,
                   bool isError);
      friend class MqttRequest;
    };

    void useMqtt();

  }  // namespace net
}  // namespace esp32m
