#pragma once

#include <array>
#include <mutex>
#include <string>

#include "esp32m/device.hpp"
#include "esp32m/dev/bfs/parameters.hpp"
#include "esp32m/events/response.hpp"

namespace esp32m::dev {

  class Bfs : public Device {
   public:
    explicit Bfs(uint8_t address = 1, uint32_t baud = 9600,
                 const char* name = nullptr);
    Bfs(const Bfs&) = delete;
    const char* name() const override { return _name.c_str(); }

   protected:
    bool initSensors() override;
    bool pollSensors() override;
    JsonDocument* getState(RequestContext& ctx) override;
    void setState(RequestContext& ctx) override;
    JsonDocument* getDeviceConfig(RequestContext& ctx) override;
    bool setDeviceConfig(RequestContext& ctx) override;
    bool handleRequest(Request& request) override;
    void handleEvent(Event& event) override;

   private:
    struct Reading {
      uint16_t raw = 0;
      uint64_t stamp = 0;
      bool seen = false, valid = false;
    };
    struct ReadJob {
      Bfs* device;
      std::unique_ptr<Response> response;
      bool faults;
      int address;
      uint32_t revision;
    };

    std::string _name;
    std::recursive_mutex _mutex;
    uint8_t _address;
    uint32_t _baud;
    bool _pending = false, _reading = false;
    uint8_t _pendingAddress = 0;
    uint32_t _pendingBaud = 0, _revision = 0;
    std::array<Reading, 8> _readings{};
    struct CachedParameter { uint16_t raw = 0; bool seen = false; };
    std::array<CachedParameter, bfs::parametersCount> _parameterCache{};
    bool _cacheDirty = false;
    uint64_t _cacheDirtyAt = 0;
    std::string _lastError;
    std::unique_ptr<JsonDocument> _communicationChange;
    Sensor _pressure, _frequency, _voltage, _current, _temperature;

    esp_err_t ready();
    esp_err_t read(uint16_t address, uint16_t& value);
    esp_err_t write(uint16_t address, uint16_t value);
    void cacheParameter(uint16_t address, uint16_t raw);
    void clearParameterCache();
    void persistParameterCache(bool force = false);
    JsonDocument* parameterCache();
    esp_err_t readChannel(size_t index);
    Sensor* channelSensor(size_t index);
    void invalidateChannel(size_t index);
    void invalidate();
    JsonDocument* state();
    esp_err_t checkProfile();
    esp_err_t checkStopped();
    esp_err_t probe(uint8_t address, uint32_t baud,
                    JsonObject details = JsonObject());
    esp_err_t resolvePending();
    esp_err_t changeCommunication(uint8_t address, uint32_t baud,
                                 bool& changed, JsonObject details);
    esp_err_t validateParameter(uint16_t address, uint16_t raw);
    void setParameter(Request& request);
    void run(Request& request);
    void startRead(Request& request, bool faults, int address);
    void readDialog(ReadJob& job);
  };

  Bfs* useBfs(uint8_t address = 1, uint32_t baud = 9600,
              const char* name = nullptr);

}  // namespace esp32m::dev
