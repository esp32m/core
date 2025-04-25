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
      Relay(const char *name, io::pin::IDigital *pin)
          : _name(name), _pinOn(pin), _pinOff(pin) {
        init();
      }
      Relay(const char *name, io::pin::IDigital *pinOn,
            io::pin::IDigital *pinOff)
          : _name(name), _pinOn(pinOn), _pinOff(pinOff) {
        init();
      }
      Relay(const char *name, io::pin::IDigital *pinOn,
            io::pin::IDigital *pinOff, io::pin::IDigital *pinSenseOn,
            io::pin::IDigital *pinSenseOff)
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
      bool isOn() {
        return state() == State::On;
      }
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
        static JsonDocument/*<JSON_ARRAY_SIZE(1)>*/ doc;
        if (doc.isNull()) {
          auto root = doc.to<JsonArray>();
          root.add("switch");
        }
        return doc.as<JsonArrayConst>();
      };
      void setState(RequestContext &ctx) override;
      JsonDocument *getState(RequestContext &ctx) override;
      bool setConfig(RequestContext &ctx) override;
      JsonDocument *getConfig(RequestContext &ctx) override;
      bool handleRequest(Request &req) override;

     private:
      const char *_name;
      io::pin::IDigital *_pinOn, *_pinOff;
      io::pin::IDigital *_pinSenseOn = nullptr, *_pinSenseOff = nullptr;
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