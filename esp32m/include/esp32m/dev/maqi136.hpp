#pragma once

#include <ArduinoJson.h>
#include <memory>
#include <mutex>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    class Maqi136 : public Device {
     public:
      enum class Units : uint8_t {
        Celsius = 0,
        Fahrenheit = 1,
        MPa = 2,
        KPa = 3,
        Pa = 4,
        Bar = 5,
        Mbar = 6,
        Psi = 7,
        MH2O = 8,
        MmH2O = 9,
      };

      Maqi136(uint8_t addr = 1, uint32_t baud = 9600);
      Maqi136(const Maqi136 &) = delete;
      const char *name() const override {
        return "MAQI-136";
      }
      float min() const {
        return _min;
      }
      float max() const {
        return _max;
      }
      float value() const {
        return _value;
      }
      float offset() const {
        return _offset;
      }
      Units units() const {
        return static_cast<Units>(_units);
      }
      uint8_t decimals() const {
        return _decimals;
      }
      /**
       * Configure an application-specific conversion from the signed R4
       * measurement register to metres:
       *
       *   metres = R4 * multiplier + offset
       *
       * The native unit/decimal conversion is used until this is called.
      */
      void calibrate(float multiplier, float offset);
      /** Override the range metadata reported by R5 and R6, in metres. */
      void setRange(float min, float max);

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      JsonDocument *getState(RequestContext &ctx) override;
      JsonDocument *getDeviceConfig(RequestContext &ctx) override;
      bool setDeviceConfig(RequestContext &ctx) override;

     private:
      static constexpr uint16_t AddressRegister = 0;
      static constexpr uint16_t BaudRegister = 1;

      static bool metersPerUnit(Units units, float &factor);
      static uint8_t precisionForStep(float step);
      static int baudCode(uint32_t baud);
      static bool validAddress(unsigned address);
      esp_err_t ready();
      esp_err_t prepareBus(uint32_t baud);
      esp_err_t probe(uint8_t address, uint32_t baud,
                      JsonObject details = JsonObject());
      esp_err_t resolvePending();
      esp_err_t changeCommunication(uint16_t reg, uint16_t raw,
                                    uint8_t address, uint32_t baud,
                                    bool &changed, JsonObject details);
      void invalidate();

      mutable std::recursive_mutex _mutex;
      uint8_t _addr, _pendingAddress = 0;
      uint32_t _baud, _pendingBaud = 0;
      uint16_t _regs[8] = {};
      uint8_t _units = 0, _decimals = 0, _precision = 2;
      float _min = 0, _max = 0, _value = 0, _offset = 0;
      float _calibrationMultiplier = 0, _calibrationOffset = 0;
      float _rangeMin = 0, _rangeMax = 0;
      bool _calibrated = false, _rangeOverridden = false;
      bool _pending = false, _restoreBus = false;
      esp_err_t _lastError = ESP_OK;
      std::unique_ptr<JsonDocument> _communicationChange;
      int64_t _stamp = -1;
      Sensor _sensor;
    };

    Maqi136 *useMaqi136(uint8_t addr = 1, uint32_t baud = 9600);
  }  // namespace dev
}  // namespace esp32m
