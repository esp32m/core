#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp32m/bus/twai.hpp>   // brings in gpio_num_t via esp_twai_onchip.h
#include <esp32m/logging.hpp>

namespace esp32m {

/**
 * Two-node Classic CAN test:
 *   - periodically transmits a uint32_t counter on txCanId
 *   - logs every received frame whose DLC == 4 as a peer counter
 *
 * The same class is used for both nodes; node identity is expressed
 * only through the constructor arguments (pins + CAN ID), which are
 * controlled by compile-time defines in main.cpp.
 */
class CanTestNode : public log::Loggable {
 public:
  /**
   * @param txPin    GPIO connected to the transceiver TX / TXD pin
   * @param rxPin    GPIO connected to the transceiver RX / RXD pin
   * @param txCanId  11-bit standard CAN ID this node transmits with
   * @param bitrate  Classic CAN bitrate in bits/s (default 250 kbit/s)
   */
  CanTestNode(gpio_num_t txPin, gpio_num_t rxPin, uint32_t txCanId,
              uint32_t bitrate = 250000);
  ~CanTestNode();

  const char* name() const override { return "can-test"; }

  // Non-copyable, non-movable
  CanTestNode(const CanTestNode &) = delete;
  CanTestNode &operator=(const CanTestNode &) = delete;

 private:
  TWAI *_twai = nullptr;
  TaskHandle_t _txTask = nullptr;
  uint32_t _txCanId;
  uint32_t _txCounter = 0;

  // Called from the TWAI RX dispatch task for every received frame
  void onRx(twai_frame_t *frame);

  // ESP-IDF task body: sends an incrementing counter once per second
  void txLoop();
};

}  // namespace esp32m
