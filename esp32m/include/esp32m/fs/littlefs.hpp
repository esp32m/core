#pragma once

#include "esp32m/config/config.hpp"
#include "esp32m/app.hpp"


namespace esp32m {
  namespace fs {
    class Littlefs : public AppObject {
     public:
      Littlefs(const Littlefs &) = delete;
      const char *name() const override {
        return "littlefs";
      }

      static Littlefs &instance();
      config::Store *newConfigStore();

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool handleRequest(Request &req) override;

     private:
      const char *_label = nullptr;
      bool _inited = false;
      Littlefs();
      bool init();
    };

    Littlefs &useLittlefs();

  }  // namespace io
}  // namespace esp32m
