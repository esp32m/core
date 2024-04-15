#pragma once

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

#include <hal/gpio_types.h>

namespace esp32m {
  namespace dev {
    class HBridge : public Device {
     public:
      enum Mode { Brake, Forward, Reverse, Off };
      const uint MaxFreq = 1000;
      HBridge(const char *name, io::IPin *fwd, io::IPin *rev)
          : _name(name), _fwd(fwd), _rev(rev) {
        init();
      };
      HBridge(const HBridge &) = delete;
      const char *name() const override {
        return _name;
      }
      esp_err_t run(Mode mode, float speed = 1);

      void setPersistent(bool p) {
        _persistent = p;
      }
      bool isPersistent() const {
        return _persistent;
      }

      float getDuty() const {
        return _duty;
      }
      uint32_t getFreq() const {
        return _freq;
      }
      esp_err_t setPwm(uint freq, float duty) {
        if (duty < 0)
          duty = 0;
        if (duty > 1)
          duty = 1;
        if (freq > MaxFreq)
          freq = MaxFreq;
        auto wasPwm = isPwm();
        _duty = duty;
        _freq = freq;
        if (wasPwm != isPwm())
          init();
        return ERR_OK;
      }
      esp_err_t setSpeed(float value) {
        return (value > 0 && value < 1) ? setPwm(_freq == 0 ? 50 : _freq, value)
                                        : setPwm(0, 0);
      }

     protected:
      const JsonVariantConst descriptor() const override {
        static StaticJsonDocument<JSON_ARRAY_SIZE(1)> doc;
        if (doc.isNull()) {
          auto root = doc.to<JsonArray>();
          root.add("h-bridge");
        }
        return doc.as<JsonArrayConst>();
      };
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      void setState(const JsonVariantConst cfg,
                    DynamicJsonDocument **result) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(RequestContext &ctx) override;

     private:
      const char *_name;
      io::IPin *_fwd, *_rev;
      float _duty = 0;
      uint _freq = 0;
      bool _persistent = true;
      Mode _mode = Off;
      float _speed = 1;
      esp_err_t init();
      esp_err_t refresh();
      esp_err_t setPins(bool fwd, bool rev);
      esp_err_t getPins(bool &fwd, bool &rev);
      bool isPwm() const {
        return _duty > 0 && _duty < 1 && _freq > 0;
      }
    };

    HBridge *useHBridge(const char *name, io::IPin *pinFwd, io::IPin *pinRev);
  }  // namespace dev
}  // namespace esp32m