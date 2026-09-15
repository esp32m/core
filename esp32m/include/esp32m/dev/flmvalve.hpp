#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {

    /** Flomarvel FH-RS Modbus motorized valve.
     *
     * Supplier-confirmed holding registers:
     *   0x0001 - commanded angle in hundredths of a degree (read/write)
     *   0x0003 - actual angle in hundredths of a degree (read-only)
     *
     * The accepted command range is 0..9000 (0..90 degrees). The supplier
     * document does not state whether 0 degrees means open or closed.
     */
    class FlomarvelValve : public Device {
     public:
      explicit FlomarvelValve(uint8_t addr = 1);
      FlomarvelValve(const FlomarvelValve&) = delete;

      const char* name() const override {
        return "FlomarvelValve";
      }

      uint8_t address() const {
        return _addr;
      }
      float targetAngle() const;
      float actualAngle() const;

      /** Set the commanded angle using Modbus function 0x06. */
      esp_err_t setTargetAngle(float angle);

      /**
       * Read holding registers 0..3 one at a time and retain raw results.
       * Registers 0 and 2 are experimental candidates from a similar valve;
       * they are not documented for the FH-RS.
       */
      esp_err_t probeRegisters();

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      bool handleRequest(Request& req) override;
      void setState(RequestContext& ctx) override;
      JsonDocument* getState(RequestContext& ctx) override;
      bool setConfig(RequestContext& ctx) override;
      JsonDocument* getConfig(RequestContext& ctx) override;

     private:
      static constexpr uint16_t CommandRegister = 0x0001;
      static constexpr uint16_t ActualRegister = 0x0003;
      static constexpr uint16_t MaxAngleRaw = 9000;

      static float rawToAngle(uint16_t raw);
      static bool isSupportedBaudrate(uint32_t baud);
      esp_err_t configureAddress(uint8_t address);
      esp_err_t configureBaudrate(uint32_t baud);
      void invalidateActual();
      void invalidateCommunicationState();

      uint8_t _addr;
      uint16_t _targetRaw = 0, _actualRaw = 0;
      unsigned long _targetStamp = 0, _actualStamp = 0;
      uint16_t _probeRegs[4] = {};
      uint8_t _probeValidMask = 0;
      unsigned long _probeStamp = 0;
      Sensor _position;
    };

    FlomarvelValve* useFlomarvelValve(uint8_t addr = 1);
  }  // namespace dev
}  // namespace esp32m
