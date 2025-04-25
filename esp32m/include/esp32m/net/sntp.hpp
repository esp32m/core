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
      std::string host(); 
      int tzOfs();
      bool synced()
      {
        return _syncedAt > 0;
      }

     protected:
      void handleEvent(Event &ev) override;
      bool handleRequest(Request &req) override;
      JsonDocument *getState(RequestContext &ctx) override;
      bool setConfig(RequestContext &ctx) override;
      JsonDocument *getConfig(RequestContext &ctx) override;

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