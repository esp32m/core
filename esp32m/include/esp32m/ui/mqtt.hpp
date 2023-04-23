#pragma once

#include "esp32m/net/mqtt.hpp"
#include "esp32m/ui/transport.hpp"

namespace esp32m {
  namespace ui {
    class Mqtt : public Transport {
     public:
      const char* name() const override {
        return "ui-mqtt";
      };
      static Mqtt& instance();

     protected:
      void init(Ui* ui) override;
      esp_err_t wsSend(uint32_t cid, const char* text) override;

     private:
      char* _requestTopic = nullptr;
      char* _responseTopic = nullptr;
      Mqtt(){};
      void respond(const char* source, int seq, const JsonVariantConst data,
                   bool isError);
      friend class MqttRequest;
    };
  }  // namespace ui
}  // namespace esp32m