#pragma once

#include <memory>
#include <string>
#include <type_traits>

#include "esp32m/device.hpp"
#include "esp32m/dev/thermostat/types.hpp"

namespace esp32m::dev {
/** Optional local policy device. Construction alone never enables actuation.
 * Keep a borrowed actuator alive longer than this wrapper. Only one wrapper
 * can attach to an actuator; a duplicate stays unavailable and cannot release
 * the first wrapper's controls or discovery attachment.
 */
class Thermostat : public Device {
 public:
  Thermostat(thermostat::Actuator& actuator, const char* name = "thermostat");
  Thermostat(std::unique_ptr<thermostat::Actuator> actuator,
             const char* name = "thermostat");
  ~Thermostat() override;
  const char* name() const override { return _name.c_str(); }

 protected:
  const JsonVariantConst descriptor() const override;
  JsonDocument* getState(RequestContext& ctx) override;
  void setState(RequestContext& ctx) override;
  JsonDocument* getDeviceConfig(RequestContext& ctx) override;
  bool setDeviceConfig(RequestContext& ctx) override;
  bool handleRequest(Request& request) override;
  bool pollSensors() override;

 private:
  class Impl;
  std::string _name;
  std::unique_ptr<Impl> _impl;
};

Thermostat* useThermostat(thermostat::Actuator& actuator,
                         const char* name = "thermostat");
Thermostat* useThermostat(std::unique_ptr<thermostat::Actuator> actuator,
                         const char* name = "thermostat");

/** Drivers implement Actuator directly; no forwarding adapter is allocated. */
template <class T, std::enable_if_t<std::is_base_of_v<Device, T> &&
                                   std::is_base_of_v<thermostat::Actuator, T>, int> = 0>
Thermostat* useThermostat(T& device, const char* name = nullptr) {
  const auto fallback = std::string(device.name()) + "_thermostat";
  return useThermostat(static_cast<thermostat::Actuator&>(device),
                       name && *name ? name : fallback.c_str());
}
}  // namespace esp32m::dev
