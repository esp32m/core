#pragma once

#include <esp_pm.h>

#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {
    class Esp32 : public Device {
     public:
      Esp32(const Esp32 &) = delete;
      const char *name() const override {
        return "ESP32";
      }
      static Esp32 &instance();

     protected:
      JsonDocument *getState(RequestContext &ctx) override;
      bool setConfig(RequestContext &ctx) override;
      JsonDocument *getConfig(RequestContext &ctx) override;

     private:
      Esp32();
      uint32_t _flashChipSize = 0, _flashChipSpeed = 0, _freeSketchSpace = 0;
      size_t _psramSize = 0;
      uint8_t _flashChipMode = 0xff;
      esp_reset_reason_t _rr;
      esp_pm_config_t _pm = {.max_freq_mhz = 240,
                                   .min_freq_mhz = 160,
                                   .light_sleep_enable = false};
    };

    void useEsp32();
  }  // namespace dev
}  // namespace esp32m
