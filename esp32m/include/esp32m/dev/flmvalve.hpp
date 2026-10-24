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
     * The accepted command range is 0..9000 (0..90 degrees). Valve controls
     * use 0 degrees as closed and 90 degrees as fully open.
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
      /** Set a normalized valve position, from 0% closed to 100% open. */
      esp_err_t setPosition(float position);
      esp_err_t open();
      esp_err_t close();

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      bool handleRequest(Request& req) override;
      void setState(RequestContext& ctx) override;
      JsonDocument* getState(RequestContext& ctx) override;
      bool setDeviceConfig(RequestContext& ctx) override;
      JsonDocument* getDeviceConfig(RequestContext& ctx) override;

     private:
      static constexpr uint16_t CommandRegister = 0x0001;
      static constexpr uint16_t ActualRegister = 0x0003;
      static constexpr uint16_t MaxAngleRaw = 9000;
      static constexpr uint16_t PositionToleranceRaw = 100;
      // Mechanical endpoint guards can stop this valve about 1.5 degrees
      // before the commanded endpoint.
      static constexpr uint16_t EndpointToleranceRaw = 200;
      // Ignore encoder jitter while deciding whether motion has stopped.
      static constexpr uint16_t MotionThresholdRaw = 25;
      static constexpr uint64_t MotionSettleMs = 3000;
      static constexpr uint64_t StateFreshnessMs = 10000;

      static float rawToAngle(uint16_t raw);
      static int rawToPosition(uint16_t raw);
      static uint64_t nowMs();
      static bool isSupportedBaudrate(uint32_t baud);
      esp_err_t configureAddress(uint8_t address);
      esp_err_t configureBaudrate(uint32_t baud);
      void invalidateActual();
      void invalidateCommunicationState();
      void expireStaleState(uint64_t now);
      void observeActual(uint16_t raw, uint64_t now);
      void observeTarget(uint16_t raw, uint64_t now);
      void updateValveState();

      uint8_t _addr;
      uint16_t _targetRaw = 0, _actualRaw = 0;
      uint64_t _targetStamp = 0, _actualStamp = 0;
      uint16_t _motionRaw = 0;
      uint64_t _motionStamp = 0;
      Sensor _position;
      ValveComponent _component;
    };

    FlomarvelValve* useFlomarvelValve(uint8_t addr = 1);
  }  // namespace dev
}  // namespace esp32m
