#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <hal/gpio_types.h>

#include <esp32m/bus/lin.hpp>
#include <esp32m/logging.hpp>

namespace esp32m {

/**
 * LIN test — master node.
 *
 * Every second the master performs two transactions on the bus:
 *   1. request(0x10) — polls the slave for its counter; the received value is
 *      forwarded to the RxCallback and logged.
 *   2. publish(0x11) — sends the master's own incrementing counter to the
 *      slave.
 *
 * Pin assignments and baud rate are supplied at construction time via the
 * compile-time defines in main.cpp.
 */
class LinTestMaster : public log::Loggable {
 public:
  LinTestMaster(gpio_num_t txPin, gpio_num_t rxPin, uint32_t baud = 19200);
  ~LinTestMaster();

  const char *name() const override { return "lin-master"; }

  LinTestMaster(const LinTestMaster &) = delete;
  LinTestMaster &operator=(const LinTestMaster &) = delete;

 private:
  lin::Master *_master = nullptr;
  TaskHandle_t _task   = nullptr;
  uint32_t _txCounter  = 0;

  void run();
};

/**
 * LIN test — slave node.
 *
 * The slave registers two handlers:
 *   - TxHandler for 0x10: responds to master requests with an incrementing
 *     counter (4 bytes, big-endian).
 *   - RxHandler for 0x11: logs counter values published by the master.
 *
 * The background task is owned by lin::Slave; this class simply
 * configures the handlers.
 */
class LinTestSlave : public log::Loggable {
 public:
  LinTestSlave(gpio_num_t txPin, gpio_num_t rxPin, uint32_t baud = 19200);
  ~LinTestSlave();

  const char *name() const override { return "lin-slave"; }

  LinTestSlave(const LinTestSlave &) = delete;
  LinTestSlave &operator=(const LinTestSlave &) = delete;

 private:
  lin::Slave *_slave  = nullptr;
  uint32_t _txCounter = 0;  // incremented only from the slave's task
};

}  // namespace esp32m
