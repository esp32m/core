#pragma once

#include <hal/gpio_types.h>

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace dev {
    class HBridge : public Device {
     public:
      enum Mode { Break, Forward, Reverse, Off };
      HBridge(const char *name, io::IPin *pinFwd, io::IPin *pinRev)
          : _name(name), _pinFwd(pinFwd), _pinRev(pinRev) {
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

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      void setState(const JsonVariantConst cfg,
                    DynamicJsonDocument **result) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;

     private:
      const char *_name;
      io::IPin *_pinFwd, *_pinRev;
      bool _persistent = true;
      Mode _mode = Off;
      float _speed = 1;
      esp_err_t init();
      esp_err_t refresh();
    };

    HBridge *useHBridge(const char *name, io::IPin *pinFwd, io::IPin *pinRev);
  }  // namespace dev
}  // namespace esp32m