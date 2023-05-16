#pragma once

#include <math.h>
#include <memory>

#include "esp32m/bus/owb.hpp"
#include "esp32m/device.hpp"

namespace esp32m {

  class Dsts;

  namespace dsts {

    enum Resolution {
      Unspecified = 0,
      R9b = 9,
      R10b = 10,
      R11b = 11,
      R12b = 12
    };

    enum Model {
      Unknown = 0x00,
      Ds18s20 = 0x10,
      Ds18b20 = 0x28,
      Ds1822 = 0x22,
      Ds1825 = 0x3B,
      Ds28ea00 = 0x42,
    };

    typedef struct {
      union {
        struct {
          uint8_t lsb;
          uint8_t msb;
          uint8_t trigger_high;
          uint8_t trigger_low;
          uint8_t configuration;
          uint8_t reserved;
          uint8_t countRemain;
          uint8_t countPerC;
          uint8_t crc;
        };
        uint8_t bytes[9];
      };
    } __attribute__((packed)) Scratchpad;

    class Core;

    class Probe {
     public:
      Probe(const owb::ROMCode code, bool persist) {
        memcpy(_code.bytes, code.bytes, sizeof(owb::ROMCode));
        owb::toString(_code, _codestr, sizeof(_codestr));
        _persistent = persist;
      }
      owb::ROMCode code() {
        return _code;
      }
      const char *codestr() const {
        return _codestr;
      }
      Model model() const;
      Resolution getResolution() const {
        return _resolution;
      }
      void setResolution(Resolution v);
      float temperature() const {
        return _temperature;
      }
      uint8_t failcount() const {
        return _failcount;
      }
      float successRate() {
        auto ta = _totalAttempts;
        if (!ta)
          return 0;
        return ((float)_totalSuccess) / ta;
      }
      bool disabled() const {
        return _failcount == std::numeric_limits<uint8_t>::max();
      }

     private:
      owb::ROMCode _code;
      char _codestr[17];
      bool _persistent;
      Resolution _resolution = Resolution::Unspecified;
      float _temperature = NAN;
      uint8_t _failcount = 0;
      uint32_t _totalAttempts = 0, _totalSuccess = 0;
      esp_err_t _error = ESP_OK;
      bool _parasite = false;
      bool _seen = false;
      bool _modified = false;
      bool check(esp_err_t err, Core *core, bool resetsFailcount,
                 const char *failmsg);
      void setTemperature(float t) {
        _temperature = t;
        _totalSuccess++;
      }
      friend class Core;
      friend class esp32m::Dsts;
    };

    class Core : public virtual log::Loggable {
     public:
      Core(Owb *owb);
      Core(const Core &) = delete;
      const char *name() const override {
        return "dsts";
      }
      esp_err_t sync();
      Probe &add(const owb::ROMCode &addr, bool persistent = true);
      Probe *find(const owb::ROMCode &addr);
      std::vector<Probe> &probes();
      bool getTemperature(Probe &probe);

     private:
      std::vector<Probe> _probes;
      std::unique_ptr<Owb> _owb;
      unsigned int _lastSync = 0;
      int _maxFailures = 10;
      esp_err_t select(const Probe *probe);
      esp_err_t select(const Probe *probe, bool &present);
      esp_err_t readScratchpad(const Probe &probe, Scratchpad &scratchpad);
      esp_err_t writeScratchpad(const Probe &probe,
                                const Scratchpad &scratchpad, bool verify);
      esp_err_t readResolution(Probe &probe);
      esp_err_t writeResolution(Probe &probe);
      esp_err_t readPowerSupply(Probe &probe);
      esp_err_t convert(const Probe &probe);
      esp_err_t convert();
      esp_err_t readTemperature(Probe &probe);
      unsigned int conversionTicks(const Probe &probe);
      esp_err_t waitForConversion(const Probe &probe);
      friend struct Probe;
    };

  }  // namespace dsts

  namespace dev {
    class Dsts : public virtual Device, public virtual dsts::Core {
     public:
      Dsts(Owb *owb);
      Dsts(const Dsts &) = delete;

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool handleRequest(Request &req) override;
      bool pollSensors() override;
      bool initSensors() override;

     private:
      int _sensorGroup;
      Sensor &getSensor(const dsts::Probe &probe);
    };

    void useDsts(gpio_num_t pin);
  }  // namespace dev
}  // namespace esp32m