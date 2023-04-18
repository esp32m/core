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
      int tzOfs() const {
        return _tzOfs;
      }

     protected:
      bool handleEvent(Event &ev) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(RequestContext &ctx) override;

     private:
      Sntp();
      bool _enabled = true;
      std::string _host;
      int _dstOfs = 0, _tzOfs = 0;
      float _interval;
      unsigned long _syncedAt = 0;
      void update();
      void synced(struct timeval *tv);
      friend void sync_time_cb(struct timeval *tv);
    };

    Sntp *useSntp();
  }  // namespace net
}  // namespace esp32m