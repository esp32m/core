#pragma once

#include <mutex>
#include <string>

#include "esp32m/device.hpp"

namespace esp32m::dev {

  /** CSF09 soil moisture/temperature sensor, protocol revision 25.07.14.
   * Configure the shared Modbus RTU master for 9600 baud, 8N1 before use.
   * The address may be changed with getConfig/setConfig. Baud changes are not
   * documented for this model; the shared bus must remain at 9600 baud.
   * config-set {address:N} programs the sensor; {connectAddress:N} only selects
   * an already-addressed sensor for this driver's polling after a read probe.
   */
  class Csf09 : public Device {
   public:
    explicit Csf09(uint8_t addr = 1, const char* name = nullptr);
    Csf09(const Csf09&) = delete;
    const char* name() const override { return _name.c_str(); }

   protected:
    bool initSensors() override;
    bool pollSensors() override;
    JsonDocument* getState(RequestContext& ctx) override;
    JsonDocument* getDeviceConfig(RequestContext& ctx) override;
    bool setDeviceConfig(RequestContext& ctx) override;

   private:
    static constexpr uint16_t AddressRegister = 0x000f;
    static bool validAddress(unsigned address);
    esp_err_t ready();
    esp_err_t probe(uint8_t address);
    esp_err_t resolvePending();
    void invalidate();

    std::string _name;
    uint8_t _addr, _pendingAddress = 0;
    std::recursive_mutex _mutex;
    esp_err_t _lastError = ESP_OK;
    bool _seen = false;
    uint64_t _stamp = 0;
    Sensor _moisture, _temperature;
  };

  Csf09* useCsf09(uint8_t addr = 1, const char* name = nullptr);

}  // namespace esp32m::dev
