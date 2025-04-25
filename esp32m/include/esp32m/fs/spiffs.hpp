#pragma once

#include <esp_spiffs.h>

#include "esp32m/config/config.hpp"
#include "esp32m/app.hpp"


namespace esp32m {
  namespace fs {
    class Spiffs : public AppObject {
     public:
      Spiffs(const Spiffs &) = delete;
      const char *name() const override {
        return "spiffs";
      }

      static Spiffs &instance();

     protected:
      JsonDocument *getState(RequestContext &ctx) override;
      bool handleRequest(Request &req) override;

     private:
      const char *_label = nullptr;
      bool _inited = false;
      Spiffs();
      bool init();
    };

    Spiffs &useSpiffs();

  }  // namespace io
}  // namespace esp32m
