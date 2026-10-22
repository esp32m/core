#pragma once

#include <hal/gpio_types.h>

#include <cstdint>
#include <memory>
#include <string>

#include "esp32m/device.hpp"
#include "esp32m/dev/thermostat/types.hpp"

namespace esp32m {
  namespace dev {

    /**
     * LF Bros diesel-heater controller for the heater's logic-side CAN tool
     * header.
     *
     * Construct this device before App::Init. The heater can decide whether a
     * tool is present while esp32m is still doing its normal application
     * initialization, so TWAI must already be able to acknowledge traffic.
     */
    class Lfbros : public Device, public thermostat::Actuator {
     public:
      enum class ControlMode : uint8_t { Temperature = 0, Power = 1 };

      struct Hardware {
        gpio_num_t txPin;
        gpio_num_t rxPin;

        Hardware(gpio_num_t tx, gpio_num_t rx) : txPin(tx), rxPin(rx) {}
      };

      struct Options {
        Hardware hardware;
        float initialTemperatureC = 18.0f;
        float initialPowerW = 2000.0f;
        ControlMode initialControlMode = ControlMode::Power;
        float minimumPowerW = 1400.0f;
        float maximumPowerW = 5000.0f;
        uint32_t pollIntervalMs = 1000;
        // Explicit installation qualification: runtime ambient/inlet is not
        // automatically the native thermostat's selected room-temperature input.
        bool thermostatNativeTemperatureQualified = false;
        float thermostatLowPumpHz = NAN;

        Options(gpio_num_t tx, gpio_num_t rx) : hardware(tx, rx) {}
        Options(Hardware hardware) : hardware(hardware) {}
      };

      explicit Lfbros(const char* name, const Options& options);
      ~Lfbros() override;
      Lfbros(const Lfbros&) = delete;

      const char* name() const override {
        return _name.c_str();
      }

      esp_err_t turn(bool on);
      /**
       * Acknowledge a latched heater fault by issuing the controller-reset
       * command used by the physical display's fault-clear path.
       *
       * This is accepted only while fresh runtime telemetry reports state 31
       * with an active fault. The CAN session is reacquired automatically
       * after the heater controller restarts.
       */
      esp_err_t clearFault();
      /**
       * Start the heater's experimental fuel-line priming (Polr) candidate.
       * This is accepted only while a valid heater session is in standby-off.
       * The mode-1 command includes special target 500, matching the manual's
       * documented number of pump strokes; live equivalence with manual Polr
       * is not yet confirmed.
       * Use turn(false) to abort; normal operation mode is restored after the
       * heater reports standby-off again.
       */
      esp_err_t startPriming();
      esp_err_t setTemperature(float valueC);
      esp_err_t setPower(float valueW);
      esp_err_t setControlMode(ControlMode mode);

      float minimumPower() const;
      float maximumPower() const;
      thermostat::Capabilities capabilities() const override;
      thermostat::Feedback snapshot() const override;
      thermostat::Result apply(const thermostat::Request& request) override;

     protected:
      void thermostatAttached(bool attached) override;
      const JsonVariantConst descriptor() const override;
      JsonDocument* getState(RequestContext& ctx) override;
      void setState(RequestContext& ctx) override;
      JsonDocument* getConfig(RequestContext& ctx) override;
      bool setConfig(RequestContext& ctx) override;
      bool handleRequest(Request& req) override;

     private:
      class Impl;
      std::string _name;
      std::unique_ptr<Impl> _impl;
    };

    /**
     * Create and pre-arm an LF Bros heater device. No GPIO defaults are
     * provided deliberately: suitable TWAI pins depend on the ESP32 variant
     * and board wiring.
     */
    Lfbros* useLfbros(const Lfbros::Options& options,
                      const char* name = "lfbros");

  }  // namespace dev
}  // namespace esp32m
