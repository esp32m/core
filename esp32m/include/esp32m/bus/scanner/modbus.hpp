#pragma once

#include <array>
#include <memory>
#include <mutex>
#include "esp32m/app.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/events/response.hpp"

namespace esp32m::bus::scanner {
  // Uses the application's configured master; never reconfigures the UART.
  class Modbus : public virtual AppObject {
   public:
    Modbus(const Modbus&) = delete;
    static Modbus& instance();
    const char* name() const override { return "modbus"; }

   protected:
    JsonDocument* getConfig(RequestContext& ctx) override;
    bool setConfig(RequestContext& ctx) override;
    JsonDocument* getState(RequestContext& ctx) override;
    bool handleRequest(Request& req) override;
    void handleEvent(Event& ev) override;

   private:
    static constexpr unsigned MaxValues = 125;
    enum class Operation { Scan, Read, Write };
    struct Options {
      unsigned from = 1, to = 247, addr = 1;
      unsigned cmd = modbus::ReadHolding, regs = 0, regc = 1;
    };
    struct Job {
      Options options;
      Operation operation;
      std::array<uint16_t, MaxValues> values{};
      std::unique_ptr<Response> response;
    };
    Modbus() = default;
    bool parse(RequestContext& ctx, Options& options);
    void run();
    void execute(Job& job);
    void publish(Job& job, bool final);
    JsonDocument* state();  // Caller holds _mutex.
    static bool isRead(unsigned cmd);
    static bool isWrite(unsigned cmd);
    static bool isBits(unsigned cmd);
    static void putOptions(JsonObject root, const Options& options);
    static void putBus(JsonObject root);

    std::mutex _mutex;
    TaskHandle_t _task = nullptr;
    Options _options, _active;
    std::unique_ptr<Job> _pending;
    Operation _operation = Operation::Scan;
    bool _busy = false, _cancel = false, _cancelled = false;
    uint32_t _jobId = 0;
    uint64_t _started = 0, _elapsed = 0;
    unsigned _scanned = 0, _total = 0, _valueCount = 0;
    esp_err_t _error = ESP_OK, _writeError = ESP_OK, _verifyError = ESP_OK;
    bool _verified = false;
    // Compact rows are [address, error, first value on success].
    std::array<esp_err_t, 248> _errors{};
    std::array<uint16_t, 248> _first{};
    std::array<uint16_t, MaxValues> _values{};
  };

  Modbus* useModbus();
}  // namespace esp32m::bus::scanner
