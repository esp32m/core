#pragma once

#include <mutex>
#include <string>

#include "esp32m/device.hpp"

namespace esp32m::dev {

  /** HSM-WT10 leak controller, supplier manual V3.2 (FC03/FC06, RTU 8N1).
   * Configure the shared Modbus master before registering this device.
   * Communication settings use the standard getConfig/setConfig API.
   */
  class HsmWt10 : public Device {
   public:
    explicit HsmWt10(uint8_t address = 5, uint32_t baud = 9600,
                     const char* name = nullptr);
    HsmWt10(const HsmWt10&) = delete;
    const char* name() const override { return _name.c_str(); }

    // value() is the last measured alarm; check isValid() before using it.
    bool value() const;
    bool isValid() const;

   protected:
    bool initSensors() override;
    bool pollSensors() override;
    JsonDocument* getState(RequestContext& ctx) override;
    JsonDocument* getDeviceConfig(RequestContext& ctx) override;
    bool setDeviceConfig(RequestContext& ctx) override;

   private:
    static constexpr uint16_t AlarmRegister = 0;
    static constexpr uint16_t AnalogRegister = 1;
    static constexpr uint16_t SensitivityRegister = 2;
    static constexpr uint16_t AddressRegister = 3;
    static constexpr uint16_t BaudRegister = 4;
    static constexpr uint16_t AlarmDelayRegister = 5;

    esp_err_t ready();
    esp_err_t read(uint16_t reg, uint16_t& value);
    esp_err_t prepareBus(uint32_t baud);
    esp_err_t probe(uint8_t address, uint32_t baud);
    esp_err_t resolvePending();
    esp_err_t changeCommunication(uint16_t reg, uint16_t raw,
                                  uint8_t address, uint32_t baud,
                                  bool& changed);
    esp_err_t changeSetting(uint16_t reg, uint16_t value, bool& changed);
    void invalidate();
    bool fresh() const;

    std::string _name;
    mutable std::recursive_mutex _mutex;
    uint8_t _address, _pendingAddress = 0;
    uint32_t _baud, _pendingBaud = 0;
    bool _pending = false, _restoreBus = false;
    bool _valid = false, _seen = false, _alarm = false;
    uint16_t _analog = 0;
    uint64_t _stamp = 0;
    esp_err_t _lastError = ESP_OK;
    BinarySensor _leak;
    Sensor _analogSensor;
  };

  HsmWt10* useHsmWt10(uint8_t address = 5, uint32_t baud = 9600,
                     const char* name = nullptr);

}  // namespace esp32m::dev
