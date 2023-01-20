#pragma once

#include "esp32m/dev/hbridge.hpp"
#include "esp32m/dev/relay.hpp"
#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

#include <hal/gpio_types.h>
#include <math.h>

namespace esp32m {
  namespace dev {
    namespace valve {
      enum Cmd { Off, Open, Close };

      class IWiring {
       public:
        virtual esp_err_t set(Cmd cmd) = 0;
      };

      class TwoRelays : public IWiring {
       public:
        TwoRelays(Relay *power, Relay *direction)
            : _power(power), _direction(direction) {
          _power->setPersistent(false);
          _direction->setPersistent(false);
        };
        esp_err_t set(Cmd cmd) override;

       private:
        std::unique_ptr<Relay> _power, _direction;
      };

      class HBridge : public IWiring {
       public:
        HBridge(dev::HBridge *hb) : _hb(hb) {
          _hb->setPersistent(false);
        };
        esp_err_t set(Cmd cmd) override;

       private:
        dev::HBridge *_hb;
      };

      class ISensor {
       public:
        enum State { Unknown, Opened, Closed, Invalid };
        virtual esp_err_t sense(State &state) = 0;
      };

      class TwoPinSensor : public ISensor {
       public:
        TwoPinSensor(io::IPin *open, io::IPin *closed);
        void setLevels(bool open, bool closed) {
          _openLevel = open;
          _closedLevel = closed;
        }
        esp_err_t sense(State &state) override;

       private:
        io::IPin *_open, *_closed;
        bool _openLevel = false, _closedLevel = false;
      };

    }  // namespace valve

    class Valve : public Device {
     public:
      enum State { Unknown, Opened, Closed, Invalid, Opening, Closing };
      Valve(const char *name, valve::IWiring *wiring, valve::ISensor *sensor);
      Valve(const Valve &) = delete;
      const char *name() const override {
        return _name;
      }
      State state() const { return _state; }
      esp_err_t turn(float value);
      esp_err_t set(valve::Cmd cmd, bool wait = true);
      void setPersistent(bool p) {
        _persistent = p;
      }
      bool isPersistent() const {
        return _persistent;
      }
      void setSamplesCount(int samples);

     protected:
      const JsonVariantConst descriptor() const override {
        static StaticJsonDocument<JSON_ARRAY_SIZE(1)> doc;
        if (doc.isNull()) {
          auto root = doc.to<JsonArray>();
          root.add("valve");
        }
        return doc.as<JsonArrayConst>();
      };
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      void setState(const JsonVariantConst cfg,
                    DynamicJsonDocument **result) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;

     private:
      const char *_name;
      std::unique_ptr<valve::IWiring> _wiring;
      std::unique_ptr<valve::ISensor> _sensor;
      TaskHandle_t _task = nullptr;
      bool _persistent = true;
      float _value = -1, _targetValue = NAN;
      int _samplesCount = 0, _siOpen = 0, _siClose = 0;
      uint16_t *_samples = nullptr;
      unsigned long _startTime = 0;
      State _state = State::Unknown;
      void setState(State state);
      esp_err_t refreshState();
      esp_err_t waitFor(State state);
      void run();
    };

    Valve *useValve(const char *name, valve::IWiring *wiring, io::IPin *isOpen,
                    io::IPin *isClosed);
    Valve *useValve(const char *name, HBridge *hb, io::IPin *isOpen,
                    io::IPin *isClosed);
    Valve *useValve(const char *name, Relay *power, Relay *direction,
                    io::IPin *isOpen, io::IPin *isClosed);
  }  // namespace dev
}  // namespace esp32m