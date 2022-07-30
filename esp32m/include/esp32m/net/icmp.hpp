#pragma once

#include "esp32m/app.hpp"

namespace esp32m {

  namespace net {

    class Ping : public AppObject {
     public:
      Ping(const Ping &) = delete;
      static Ping &instance();
      const char *name() const override {
        return "ping";
      }

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;

     private:
      Ping() {}
    };

    Ping *usePing();
  }  // namespace net
}  // namespace esp32m