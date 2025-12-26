#pragma once

#include <memory>

#include "esp32m/bus/i2c/master.hpp"
#include "esp32m/device.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {
  class Ina219;

  namespace ina219 {

    static const uint8_t DefaultAddress = 0x40;
    static const uint16_t DefaultConfig = 0x399f;

    enum BusRange { V16 = 0, V32 };

    enum Gain {
      x1,
      x05,
      x025,
      x0125,
    };

    enum Resolution {
      S1B9 = 0,
      S1B10 = 1,
      S1B11 = 2,
      S1B12 = 3,
      S2B12 = 9,
      S4B12 = 10,
      S8B12 = 11,
      S16B12 = 12,
      S32B12 = 13,
      S6B12 = 14,
      S128B12 = 15
    };

    enum Mode {
      Down = 0,
      TrigShunt,
      TrigBus,
      TrigShuntBus,
      Disabled,
      ContShunt,
      ContBus,
      ContShuntBus
    };

    enum Register { Config, Shunt, Bus, Power, Current, Calibration };

    struct Settings {
     public:
      void setRange(BusRange value);
      BusRange getRange() const;
      void setGain(Gain value);
      Gain getGain() const;
      void setURes(Resolution value);
      Resolution getURes() const;
      void setIRes(Resolution value);
      Resolution getIRes() const;
      void setMode(Mode value);
      Mode getMode() const;
      void setIMax(float value);
      float getIMax() const;
      void setShunt(float value);
      float getShunt() const;

     private:
      uint16_t _config = DefaultConfig;
      uint16_t _shunt = 100;  // mOhm
      float _imax = 2;
      bool _modified = true;
      friend class Core;
    };

    class Core : public virtual log::Loggable {
     public:
      Core(i2c::MasterDev* i2c);
      Core(const Core&) = delete;
      const char* name() const override {
        return "INA219";
      }
      Settings& settings() {
        return _settings;
      }
      esp_err_t reset();
      esp_err_t sync(bool force = false);
      esp_err_t measure();
      esp_err_t getBusVoltage(float& value);
      esp_err_t getShuntVoltage(float& value);
      esp_err_t getCurrent(float& value);
      esp_err_t getPower(float& value);

     protected:
      std::unique_ptr<i2c::MasterDev> _i2c;

     private:
      Settings _settings;
      float _lsbI = 0, _lsbP = 0;
      friend class esp32m::Ina219;
    };

  }  // namespace ina219

  namespace dev {
    class Ina219 : public virtual Device, public virtual ina219::Core {
     public:
      Ina219(i2c::MasterDev* i2c);
      Ina219(const Ina219&) = delete;

     protected:
      JsonDocument* getState(RequestContext& ctx) override;
      bool pollSensors() override;
      bool initSensors() override;

     private:
      Sensor _voltage, _current, _power;
    };

    void useIna219(uint8_t addr = ina219::DefaultAddress);

  }  // namespace dev
}  // namespace esp32m
