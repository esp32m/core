#include "lin_test.hpp"

#include <cinttypes>
#include <driver/uart.h>
#include <esp_err.h>

namespace esp32m {

  // Frame descriptors shared by master and slave.
  // 0x10: slave → master  (slave provides 4-byte counter on request)
  // 0x11: master → slave  (master publishes 4-byte counter)
  static constexpr lin::Frame kSlaveToMaster = {0x10, 4, lin::Checksum::Enhanced};
  static constexpr lin::Frame kMasterToSlave = {0x11, 4, lin::Checksum::Enhanced};

  // -------------------------------------------------------------------------
  // LinTestMaster
  // -------------------------------------------------------------------------

  LinTestMaster::LinTestMaster(gpio_num_t txPin, gpio_num_t rxPin,
                                uint32_t baud) {
    _master = new lin::Master(
        UART_NUM_1, (int)txPin, (int)rxPin, baud,
        // RxCallback: invoked when a slave response has been verified.
        [this](uint8_t pid, const uint8_t *data, uint8_t len) {
          if (len < 4)
            return;
          uint32_t val = ((uint32_t)data[0] << 24) |
                         ((uint32_t)data[1] << 16) |
                         ((uint32_t)data[2] << 8) | data[3];
          logI("rx  pid=0x%02x  slave_counter=%" PRIu32, (unsigned)pid, val);
        });

    xTaskCreate(
        [](void *self) { static_cast<LinTestMaster *>(self)->run(); },
        "m/lin/tx", 3072, this, 5, &_task);
  }

  LinTestMaster::~LinTestMaster() {
    if (_task) {
      vTaskDelete(_task);
      _task = nullptr;
    }
    delete _master;
    _master = nullptr;
  }

  void LinTestMaster::run() {
    for (;;) {
      vTaskDelay(pdMS_TO_TICKS(1000));

      // 1. Request: poll the slave's counter.
      esp_err_t err = _master->request(kSlaveToMaster, /*timeoutMs=*/50);
      if (err != ESP_OK)
        logW("request 0x%02x: %s", (unsigned)kSlaveToMaster.id,
             esp_err_to_name(err));

      // 2. Publish: send our own counter to the slave.
      uint32_t val    = _txCounter++;
      uint8_t buf[4] = {
          (uint8_t)(val >> 24),
          (uint8_t)(val >> 16),
          (uint8_t)(val >> 8),
          (uint8_t)(val),
      };
      err = _master->publish(kMasterToSlave, buf);
      if (err != ESP_OK)
        logW("publish 0x%02x: %s", (unsigned)kMasterToSlave.id,
             esp_err_to_name(err));
      else
        logI("tx  pid=0x%02x  master_counter=%" PRIu32,
             (unsigned)lin::computePid(kMasterToSlave.id), val);
    }
  }

  // -------------------------------------------------------------------------
  // LinTestSlave
  // -------------------------------------------------------------------------

  LinTestSlave::LinTestSlave(gpio_num_t txPin, gpio_num_t rxPin,
                              uint32_t baud) {
    _slave = new lin::Slave(UART_NUM_1, (int)txPin, (int)rxPin, baud);

    // Respond to master requests with our incrementing counter.
    _slave->setTxHandler(
        kSlaveToMaster,
        [this](uint8_t pid, uint8_t *data, uint8_t dataLen) -> bool {
          uint32_t val = _txCounter++;
          data[0]      = (uint8_t)(val >> 24);
          data[1]      = (uint8_t)(val >> 16);
          data[2]      = (uint8_t)(val >> 8);
          data[3]      = (uint8_t)(val);
          logI("tx  pid=0x%02x  slave_counter=%" PRIu32, (unsigned)pid, val);
          return true;
        });

    // Log counter values published by the master.
    _slave->setRxHandler(
        kMasterToSlave,
        [this](uint8_t pid, const uint8_t *data, uint8_t dataLen) {
          uint32_t val = ((uint32_t)data[0] << 24) |
                         ((uint32_t)data[1] << 16) |
                         ((uint32_t)data[2] << 8) | data[3];
          logI("rx  pid=0x%02x  master_counter=%" PRIu32, (unsigned)pid, val);
        });
  }

  LinTestSlave::~LinTestSlave() {
    delete _slave;
    _slave = nullptr;
  }

}  // namespace esp32m
