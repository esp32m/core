#pragma once

#include "esp32m/device.hpp"
#include "esp32m/debug/rs485/stats.hpp"

namespace esp32m::debug {
  // Passive diagnostics for the shared Modbus master, including raw exchanges.
  // Register after configureSerial(); publishes standard Sensor components.
  class Rs485 : public Device, public modbus::Observer {
   public:
    static Rs485& instance();
    const char* name() const override { return "rs485-health"; }
    void prepareTransaction(bool raw, uint8_t address) override;
    void onTransaction(const modbus::Transaction& transaction) override;

   protected:
    JsonDocument* getState(RequestContext& ctx) override;
    bool handleRequest(Request& request) override;

   private:
    Rs485();
    void publishSensors();
    enum Metric {
      Health, TotalHealth, ErrorRate, TimeoutRate, InvalidRate, OtherRate,
      RequestRate, MeanMs, SuccessMs, MaxMs, AttemptsTotal, ErrorsTotal,
      Session, MetricCount
    };
    rs485::Stats _stats;
    std::mutex _publishMutex;
    TaskHandle_t _task = nullptr;
    dev::Sensor _sensors[MetricCount];
  };

  Rs485* useRs485();
}  // namespace esp32m::debug
