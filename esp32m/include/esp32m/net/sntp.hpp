#pragma once

#include <string>
#include "esp32m/app.hpp"
#include <lwip/apps/sntp.h>

namespace esp32m {

  namespace net {
    class Sntp : public AppObject {
     public:
      Sntp(const Sntp &) = delete;
      static Sntp &instance();
      const char *name() const override {
        return "sntp";
      }

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;

     private:
      Sntp() {}
      bool _enabled = true;
      std::string _host;
      int _dstOfs = 0, _tzOfs = 0;
      void update();
    };

    Sntp *useSntp();
  }  // namespace net
}  // namespace esp32m