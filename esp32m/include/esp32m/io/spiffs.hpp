#pragma once

#include <esp_spiffs.h>

#include "esp32m/config/store.hpp"
#include "esp32m/app.hpp"


namespace esp32m {
  namespace io {
    class Spiffs : public AppObject {
     public:
      Spiffs(const Spiffs &) = delete;
      const char *name() const override {
        return "spiffs";
      }

      static Spiffs &instance();
      ConfigStore *newConfigStore();

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool handleRequest(Request &req) override;

     private:
      const char *_label = nullptr;
      bool _inited = false;
      Spiffs(){};
      bool init();
    };

    Spiffs &useSpiffs();

  }  // namespace io
}  // namespace esp32m
