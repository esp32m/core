#pragma once

#include <hal/gpio_types.h>

#include "esp32m/device.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace dev {
    class Relay : public Device {
     public:
      enum class State { Unknown = 0, On = 1, Off = 2 };
      enum class Pin {
        On,
        Off,
        SenseOn,
        SenseOff,
      };
      Relay(const char *name, io::IPin *pin)
          : _name(name), _pinOn(pin), _pinOff(pin) {
        init();
      }
      Relay(const char *name, io::IPin *pinOn, io::IPin *pinOff)
          : _name(name), _pinOn(pinOn), _pinOff(pinOff) {
        init();
      }
      Relay(const char *name, io::IPin *pinOn, io::IPin *pinOff,
            io::IPin *pinSenseOn, io::IPin *pinSenseOff)
          : _name(name),
            _pinOn(pinOn),
            _pinOff(pinOff),
            _pinSenseOn(pinSenseOn),
            _pinSenseOff(pinSenseOff) {
        init();
      }
      Relay(const Relay &) = delete;
      const char *name() const override {
        return _name;
      }
      State state() {
        return refreshState();
      }
      const char *stateName();
      esp_err_t turn(bool on);
      esp_err_t turn(const char *action);
      void setPersistent(bool p) {
        _persistent = p;
      }
      bool isPersistent() const {
        return _persistent;
      }
      void setOnLevel(Pin pin, bool level);
      static const char *toString(State s);

     protected:
      const JsonVariantConst descriptor() const override {
        static StaticJsonDocument<JSON_ARRAY_SIZE(1)> doc;
        if (doc.isNull()) {
          auto root = doc.to<JsonArray>();
          root.add("switch");
        }
        return doc.as<JsonArrayConst>();
      };
      void setState(const JsonVariantConst cfg,
                    DynamicJsonDocument **result) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;

     private:
      const char *_name;
      io::IPin *_pinOn, *_pinOff;
      io::IPin *_pinSenseOn = nullptr, *_pinSenseOff = nullptr;
      uint8_t _levels = 0;
      bool _persistent = true;
      State _state = State::Unknown;
      void setState(State state);
      bool getLevel(Pin pin, bool on);
      void init();
      State refreshState();
    };

    Relay *useRelay(const char *name, io::IPin *pin);
    Relay *useRelay(const char *name, io::IPin *pinOn, io::IPin *pinOff);
    Relay *useRelay(const char *name, io::IPin *pinOn, io::IPin *pinOff,
                    io::IPin *pinSenseOn, io::IPin *pinSenseOff);
  }  // namespace dev
}  // namespace esp32m