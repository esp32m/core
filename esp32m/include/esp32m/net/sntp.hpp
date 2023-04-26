#pragma once

#include <string>
#include "esp32m/app.hpp"

namespace esp32m {

  namespace net {
    class Sntp : public AppObject {
     public:
      Sntp(const Sntp &) = delete;
      static Sntp &instance();
      const char *name() const override {
        return "sntp";
      }
      std::string host() const {
        return _host;
      }

     protected:
      void handleEvent(Event &ev) override;
      bool handleRequest(Request &req) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(RequestContext &ctx) override;

     private:
      Sntp();
      bool _enabled = true;
      std::string _host, _tzr, _tze;
      float _interval;
      unsigned long _syncedAt = 0;
      void update();
      void synced(struct timeval *tv);
      friend void sync_time_cb(struct timeval *tv);
    };

    Sntp *useSntp();
  }  // namespace net
}  // namespace esp32m