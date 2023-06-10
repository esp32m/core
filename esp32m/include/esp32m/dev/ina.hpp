// Ported from https://github.com/Zanduino/INA
#pragma once

#include <memory>

#include "esp32m/bus/i2c.hpp"
#include "esp32m/device.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {
  namespace ina {

    enum Flags {
      None = 0,
      ConfigLoaded = BIT0,
      Inited = BIT1,
      Calibrated = BIT2,
      AvgChanged = BIT3,
    };

    ENUM_FLAG_OPERATORS(Flags)

    enum Type {
      Unknown,
      Ina219,
      Ina226,
      Ina230,
      Ina231,
      Ina260,
      Ina3221_0,
      Ina3221_1,
      Ina3221_2
    };

    enum Register {
      Conf = 0,
      Shunt,
      Bus,
      Power,
      Current,
      Calibration,
      MaskEnable,
      AlertLimit,
      ManufacturerId = 0xFE,
      DieId = 0xFF
    };

    enum Mode {
      Down = 0,
      TriggeredShunt,
      TriggeredBus,
      TriggeredBoth,
      Disabled,
      ContinouosShunt,
      ContinouosBus,
      ContinouosBoth
    };

    typedef union {
      struct {
        union {
          uint16_t mode : 3;
          struct {
            uint16_t shunt : 1;  ///< Enable/Disable shunt measure    // LSB
            uint16_t bus : 1;    ///< Enable/Disable bus measure
            uint16_t cont : 1;   ///< Single shot measure or continious mode
          } modeBits;
        };
        union {
          struct {
            uint16_t sadc : 4;
            uint16_t badc : 4;
            uint16_t pg : 2;
            uint16_t brng : 1;
            uint16_t _ : 1;
          } ina219;
          struct {
            uint16_t vshct : 3;   ///< Shunt voltage conversion time
            uint16_t vbusct : 3;  ///< Bus voltage conversion time
            uint16_t
                avg : 3;  ///< number of sample collected and averaged together
            uint16_t _ : 3;
          } ina226_23x;
          struct {
            uint16_t ishct : 3;   ///< Shunt voltage conversion time
            uint16_t vbusct : 3;  ///< Bus voltage conversion time
            uint16_t
                avg : 3;  ///< number of sample collected and averaged together
            uint16_t _ : 3;
          } ina260;
          struct {
            uint16_t vshct : 3;   ///< Shunt voltage conversion time
            uint16_t vbusct : 3;  ///< Bus voltage conversion time
            uint16_t
                avg : 3;  ///< number of sample collected and averaged together
            uint16_t ch3 : 1;  ///< Enable/Disable channel 3
            uint16_t ch2 : 1;  ///< Enable/Disable channel 2
            uint16_t ch1 : 1;  ///< Enable/Disable channel 1
          } ina3221;
        };
        uint16_t rst : 1;  ///< Set this bit to 1 to reset device  // MSB
      };
      uint16_t value;
    } Config;

    class Core : public virtual log::Loggable {
     public:
      Core(I2C* i2c, const char* name = nullptr, Type type = Type::Unknown);
      Core(const Core&) = delete;
      const char* name() const override;
      Type type() const {
        return _type;
      }

      esp_err_t sync(bool force = false);
      esp_err_t reset();

      bool is3221() const;

      Mode getMode() const;
      void setMode(Mode value);
      uint16_t getShuntMilliOhms() const;
      void setShuntMilliOhms(uint16_t value);
      uint32_t getMaxCurrentMilliAmps() const;
      void setMaxCurrentMilliAmps(uint32_t value);
      uint16_t getAveraging() const;
      void setAveraging(uint16_t samples);

      esp_err_t trigger();
      esp_err_t getBusRaw(uint16_t& value);
      esp_err_t getShuntRaw(int16_t& value);

      esp_err_t getBusVolts(float& value);
      esp_err_t getShuntVolts(float& value);
      esp_err_t getAmps(float& value);
      esp_err_t getWatts(float& value);

     protected:
      std::unique_ptr<I2C> _i2c;

     private:
      const char* _name;
      Type _type;
      Flags _flags = Flags::None;
      Mode _mode = Mode::ContinouosBoth;
      uint16_t _avgSamples = 0;
      int8_t _regBus, _regShunt, _regCurrent;
      uint16_t _lsbShunt, _lsbBus;
      uint32_t _lsbCurrent, _lsbPower;

      Config _config = {};
      uint16_t _shunt_mOhm = 100;
      uint32_t _maxCurrent_mA = 2000;
      esp_err_t detect();
      esp_err_t init();
      esp_err_t calibrate();
      esp_err_t changeConfig();
    };

  }  // namespace ina

  namespace dev {
    class Ina : public virtual Device, public virtual ina::Core {
     public:
      Ina(I2C* i2c);
      Ina(const Ina&) = delete;

     protected:
      DynamicJsonDocument* getState(const JsonVariantConst args) override;
      bool pollSensors() override;
      bool initSensors() override;

     private:
      unsigned long _stamp = 0;
      Sensor _voltage, _current, _power;
    };

    Ina* useIna(uint8_t addr);

  }  // namespace dev

}  // namespace esp32m