#pragma once

#include <driver/uart.h>
#include <hal/gpio_types.h>
#include <memory>
#include <string>

#include "esp32m/device.hpp"
#include "esp32m/dev/cdhx/protocol.hpp"
#include "esp32m/dev/thermostat/types.hpp"

namespace esp32m::dev {

/** CDHX1 (also called CDX1) / H45214A0 split-DATA display interposer.
 * External 5-V-tolerant receivers and non-inverting sink/release transmitters
 * are required. Never connect heater DATA directly to an ESP32 GPIO.
 * Both UARTs must be dedicated to this device. No tuning defaults are invented:
 * every transmission is derived from a complete, CRC-valid physical packet.
 */
class Cdhx : public Device, public thermostat::Actuator {
 public:
  enum class Mode { Temperature, FixedPower };
  struct Configuration {
    Mode mode = Mode::Temperature;
    int value = 0;
    // Ignored compatibility fields. Production settings accept explicit edits
    // regardless of the age or revision of the caller's displayed values.
    uint64_t expectedRevision = 0;
    uint64_t acceptBeforeUptimeMs = 0;
    std::string expectedBootId;
  };
  struct Endpoint {
    gpio_num_t rxPin;
    gpio_num_t txPin;
    uart_port_t uart;
  };
  struct Options {
    Endpoint heater;
    Endpoint display;
    const char* model = "CDHX1 / H45214A0";
    uint32_t freshnessMs = 2000;
    uint32_t responseTimeoutMs = 200;
    uint32_t quietBeforeArmMs = 20;
    // Production draft 2: GPIO15 -> Q1 -> U8, HIGH=intercept,
    // LOW/high-impedance=bypass. Leave NC for a relay-free breadboard.
    gpio_num_t bypassPin = GPIO_NUM_NC;
    int interceptLevel = 1;
    uint32_t bypassSettleMs = 10;
    // Disabled unless a range has been validated for this installation.
    // These are raw request-byte values, never watts or a universal Hz scale.
    int fixedPowerMinimumRaw = -1;
    int fixedPowerMaximumRaw = -1;
    // Installation-qualified steady-running low-output threshold. Disabled by
    // default: configured minimum pump frequency is not the native output floor.
    int thermostatLowPumpDecihz = -1;
    // Optional binding of the caller-qualified raw range to captured tuning.
    bool requireFixedPowerTemplate = false;
    cdhx::protocol::Frame fixedPowerTemplate{};
    Options(Endpoint heaterSide, Endpoint displaySide)
        : heater(heaterSide), display(displaySide) {}
  };

  explicit Cdhx(const char* name, const Options& options);
  ~Cdhx() override;
  Cdhx(const Cdhx&) = delete;
  const char* name() const override { return _name.c_str(); }
  // Accepted means queued for the next physical request, not acknowledged.
  // Start/Stop are single pulses and are never retried after uncertainty.
  esp_err_t turn(bool on);
  esp_err_t setTemperature(int temperatureC);
  esp_err_t setFixedPower(int raw);
  // Persistent mode/value authority; saved settings do not expire.
  // Mode changes require standby.
  esp_err_t setConfiguration(const Configuration& configuration);
  // Releases temporary research ownership only; persisted settings stay owned.
  esp_err_t releaseOverride();
  // Explicitly re-arm a faulted bridge. Saved settings resume; one-shot
  // commands are cleared and never replayed.
  esp_err_t resetBridge();
  // Requires bypass hardware. true joins stock DATA with both TX released;
  // false requalifies stock traffic before intercepting and resumes saved
  // settings. One-shot commands are cleared and never replayed.
  esp_err_t setBypass(bool enabled);

  // Typed local thermostat seam. Requests are transient and never persist
  // operating settings. The revision guard rejects a stale supervisor tick
  // after a physical display edit, raw command, reset or transport interruption.
  thermostat::Capabilities capabilities() const override;
  thermostat::Feedback snapshot() const override;
  thermostat::Result apply(const thermostat::Request& request) override;

 protected:
  bool handleRequest(Request& request) override;
  const JsonVariantConst descriptor() const override;
  JsonDocument* getState(RequestContext& ctx) override;
  void setState(RequestContext& ctx) override;
  JsonDocument* getConfig(RequestContext& ctx) override;
  bool setConfig(RequestContext& ctx) override;
  bool pollSensors() override;

 private:
  class Impl;
  std::string _name;
  std::unique_ptr<Impl> _impl;
};

Cdhx* useCdhx(const Cdhx::Options& options, const char* name = "cdhx");

}  // namespace esp32m::dev
