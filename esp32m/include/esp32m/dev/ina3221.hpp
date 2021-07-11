#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <memory>

#include "esp32m/bus/i2c.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  class Ina3221;

  namespace ina3221 {

    static const uint8_t DefaultAddress = 0x40;
    static const uint16_t DefaultConfig = 0x7127;
    static const uint16_t DefaultMask = 0x0002;

    typedef union {
      struct {
        uint16_t esht : 1;  ///< Enable/Disable shunt measure    // LSB
        uint16_t ebus : 1;  ///< Enable/Disable bus measure
        uint16_t mode : 1;  ///< Single shot measure or continious mode
        uint16_t vsht : 3;  ///< Shunt voltage conversion time
        uint16_t vbus : 3;  ///< Bus voltage conversion time
        uint16_t avg : 3;  ///< number of sample collected and averaged together
        uint16_t ch3 : 1;  ///< Enable/Disable channel 3
        uint16_t ch2 : 1;  ///< Enable/Disable channel 2
        uint16_t ch1 : 1;  ///< Enable/Disable channel 1
        uint16_t rst : 1;  ///< Set this bit to 1 to reset device  // MSB
      };
      uint16_t value;
    } Config;

    typedef union {
      struct {
        uint16_t cvrf : 1;  ///< Conversion ready flag (1: ready)   // LSB
        uint16_t tcf : 1;   ///< Timing control flag
        uint16_t pvf : 1;   ///< Power valid flag
        uint16_t wf : 3;  ///< Warning alert flag (Read mask to clear) (order :
                          ///< Channel1:channel2:channel3)
        uint16_t sf : 1;  ///< Sum alert flag (Read mask to clear)
        uint16_t cf : 3;  ///< Critical alert flag (Read mask to clear) (order :
                          ///< Channel1:channel2:channel3)
        uint16_t cen : 1;   ///< Critical alert latch (1:enable)
        uint16_t wen : 1;   ///< Warning alert latch (1:enable)
        uint16_t scc3 : 1;  ///< channel 3 sum (1:enable)
        uint16_t scc2 : 1;  ///< channel 2 sum (1:enable)
        uint16_t scc1 : 1;  ///< channel 1 sum (1:enable)
        uint16_t : 1;       ///< Reserved         //MSB
      };
      uint16_t value;
    } Mask;

    enum Channel { First, Second, Third, Sum, Max = Sum };

    inline Channel operator++(Channel &d, int) {
      d = static_cast<Channel>((static_cast<int>(d) + 1));
      return d;
    };

    enum Average { A1, A4, A16, A64, A128, A256, A512, A1024 };

    enum ConversionTime { T140, T204, T332, T588, T1100, T2116, T4156, T8244 };

    enum Register {
      ConfigValue = 0x00,
      ShuntVoltage1,
      BusVoltage1,
      ShuntVoltage2,
      BusVoltage2,
      ShuntVoltage3,
      BusVoltage3,
      CriticalLimit1,
      WarningLimit1,
      CriticalLimit2,
      WarningLimit2,
      CriticalLimit3,
      WarningLimit3,
      ShuntVoltageSum,
      ShuntVoltageSumLimit,
      MaskValue,
      ValidPowerUpper,
      ValidPowerLower,
      ManufacturererId = 0xFE,
      DieId = -0xFF
    };

    struct Settings {
     public:
      void setMode(bool continuous, bool bus, bool shunt);
      void enableChannels(bool c1, bool c2, bool c3);
      void enableSumChannels(bool c1, bool c2, bool c3);
      void enableLatch(bool c1, bool warning, bool critical);
      void setAverage(Average avg);
      void setBusConversionTime(ConversionTime ct);
      void setShuntConversionTime(ConversionTime ct);
      void setShunts(uint16_t s1, uint16_t s2, uint16_t s3);
      void setCriticalAlert(Channel ch, float critical = NAN);
      void setWarningAlert(Channel ch, float warning = NAN);
      void setSumWarning(float sumWarning = NAN);
      void setPowerValidRange(float upper = NAN, float lower = NAN);
      bool isEnabled(Channel ch) const;
      Config config() const {
        return _config;
      }

     private:
      uint16_t _shunts[Channel::Max] = {100, 100, 100};
      Config _config = {.value = DefaultConfig};
      Mask _mask = {.value = DefaultMask};
      float _criticalAlerts[Channel::Max] = {NAN, NAN, NAN};
      float _warningAlerts[Channel::Max] = {NAN, NAN, NAN};
      float _sumWarningAlert = NAN, _powerValidUpper = NAN,
            _powerValidLower = NAN;
      bool _modified = true;
      friend class Core;
    };

    class Core : public virtual log::Loggable {
     public:
      Core(I2C *i2c);
      Core(const Core &) = delete;
      const char *name() const override {
        return "INA3221";
      }
      Settings &settings() {
        return _settings;
      }
      esp_err_t sync(bool force = false);
      esp_err_t reset();
      esp_err_t readShunt(Channel ch, float *voltage, float *current);
      esp_err_t readBus(Channel ch, float &voltage);

     protected:
      std::unique_ptr<I2C> _i2c;

     private:
      Settings _settings;
      friend class esp32m::Ina3221;
    };

  }  // namespace ina3221

  namespace dev {
    class Ina3221 : public virtual Device, public virtual ina3221::Core {
     public:
      Ina3221(uint8_t address = ina3221::DefaultAddress);
      Ina3221(I2C *i2c);
      Ina3221(const Ina3221 &) = delete;
      const char *name() const override {
        return "INA3221";
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      void setState(const JsonVariantConst cfg,
                    DynamicJsonDocument **result) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;
      static StaticJsonDocument<JSON_ARRAY_SIZE(4) + JSON_OBJECT_SIZE(1) * 4>
          _channelProps;
      static void staticInit();
    };

    void useIna3221(uint8_t address = ina3221::DefaultAddress);
  }  // namespace dev
}  // namespace esp32m