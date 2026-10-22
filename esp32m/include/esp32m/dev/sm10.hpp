#pragma once

#include <mutex>
#include <string>

#include "esp32m/device.hpp"
#include "esp32m/dev/sm10/protocol.hpp"

namespace esp32m::dev {

  // SM10 application protocols only. Uses the configured Modbus UART and
  // restores that bus's settings after each proprietary transaction.
  class Sm10 : public Device {
   public:
    Sm10(uint8_t address = 1, uint32_t baud = 9600,
         const char* name = nullptr);
    Sm10(const Sm10&) = delete;
    const char* name() const override { return _name.c_str(); }

   protected:
    bool initSensors() override;
    bool pollSensors() override;
    JsonDocument* getState(RequestContext& ctx) override;
    JsonDocument* getDeviceConfig(RequestContext& ctx) override;
    bool setDeviceConfig(RequestContext& ctx) override;
    bool handleRequest(Request& request) override;

   private:
    std::string _name;
    std::recursive_mutex _mutex;
    sm10::protocol::Identity _identity{};
    sm10::protocol::Fine _fine{};
    sm10::protocol::Status _status{};
    sm10::protocol::Rtc _rtc{};
    uint32_t _baud;
    bool _identified = false, _hasReading = false, _hasStatus = false;
    bool _pending = false;
    uint8_t _pendingAddress = 0;
    uint32_t _pendingBaud = 0;
    double _flowValue = 0;
    uint64_t _stamp = 0, _rtcStamp = 0, _nextRtcCheck = 0;
    uint64_t _nextDiscoveryAt = 0;
    const char* _rtcSync = "unknown";
    std::string _lastError;
    Sensor _flow, _total, _temperature, _voltage;

    esp_err_t readEb(sm10::protocol::Command command,
                     sm10::protocol::EbReply& reply);
    esp_err_t writeEb(const sm10::protocol::Frame& frame,
                      sm10::protocol::Command command,
                      const sm10::protocol::Tuple& expected);
    esp_err_t identify();
    esp_err_t discover();
    esp_err_t resolvePending();
    esp_err_t readFine(sm10::protocol::Fine& result);
    esp_err_t readFlow(double& flow);
    esp_err_t readRtc();
    esp_err_t checkRtc();
    esp_err_t checkIdle(sm10::protocol::Fine& result);
    esp_err_t changeMeter(uint8_t address, uint32_t baud,
                          const uint32_t* totalCoarse, bool& configChanged);
    void publishReadings();
    void invalidate(esp_err_t error, const char* operation);
    JsonDocument* state();
  };

  Sm10* useSm10(uint8_t address = 1, uint32_t baud = 9600,
               const char* name = nullptr);

}  // namespace esp32m::dev
