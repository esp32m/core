#pragma once

#include "esp32m/app.hpp"
#include "esp32m/bus/owb.hpp"

namespace esp32m {
  namespace bus {
    namespace scanner {

      class Owb : virtual AppObject {
       public:
        Owb(const Owb &) = delete;
        static Owb &instance();
        const char *name() const override {
          return "scanner-owb";
        }

        const char *interactiveName() const override {
          return "owb";
        }

       protected:
        DynamicJsonDocument *getState(const JsonVariantConst args) override;
        bool handleRequest(Request &req) override;
        void handleEvent(Event &ev) override;

       private:
        TaskHandle_t _task = nullptr;
        Response *_pendingResponse = nullptr;
        int _pin = 4;
        uint8_t _maxDevices = 16;
        Owb(){};
        void run();
      };

      Owb *useOwb();
    }  // namespace scanner
  }    // namespace bus
}  // namespace esp32m
