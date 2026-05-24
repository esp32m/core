#include "can_test.hpp"

#include <cinttypes>
#include <esp_err.h>

namespace esp32m {

CanTestNode::CanTestNode(gpio_num_t txPin, gpio_num_t rxPin, uint32_t txCanId,
                         uint32_t bitrate)
    : _txCanId(txCanId) {
  twai_onchip_node_config_t node_config{};
  node_config.io_cfg.tx = txPin;
  node_config.io_cfg.rx = rxPin;
  node_config.io_cfg.quanta_clk_out = GPIO_NUM_NC;
  node_config.io_cfg.bus_off_indicator = GPIO_NUM_NC;
  node_config.bit_timing.bitrate = bitrate;
  node_config.timestamp_resolution_hz = 0;  // timestamps not needed
  node_config.tx_queue_depth = 8;
  // flags default to 0: normal mode (no listen-only, no loopback)

  twai_node_handle_t node_hdl;
  ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_hdl));

  _twai = new TWAI(node_hdl, [this](twai_frame_t *frame) { onRx(frame); });

  ESP_ERROR_CHECK(_twai->enable(true));

  xTaskCreate([](void *self) { static_cast<CanTestNode *>(self)->txLoop(); },
              "m/can/tx", 3072, this, 5, &_txTask);
}

CanTestNode::~CanTestNode() {
  if (_txTask) {
    vTaskDelete(_txTask);
    _txTask = nullptr;
  }
  delete _twai;
  _twai = nullptr;
}

void CanTestNode::onRx(twai_frame_t *frame) {
  // Only handle 4-byte data frames (our counter payload)
  if (frame->header.dlc != 4)
    return;

  uint32_t value = ((uint32_t)frame->buffer[0] << 24) |
                   ((uint32_t)frame->buffer[1] << 16) |
                   ((uint32_t)frame->buffer[2] << 8) |
                   (uint32_t)frame->buffer[3];

  logI("rx  id=0x%03" PRIx32 "  counter=%" PRIu32, frame->header.id, value);
}

void CanTestNode::txLoop() {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));

    uint32_t value = _txCounter++;

    uint8_t buf[4] = {
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)(value),
    };

    twai_frame_t frame{};
    frame.header.id = _txCanId;
    frame.header.ide = 0;  // standard 11-bit frame
    frame.header.rtr = 0;  // data frame
    frame.header.dlc = 4;
    frame.buffer = buf;
    frame.buffer_len = sizeof(buf);

    esp_err_t err = _twai->transmit(&frame, 100);
    if (err != ESP_OK)
      logW("tx failed: %s", esp_err_to_name(err));
    else
      logI("tx  id=0x%03" PRIx32 "  counter=%" PRIu32, _txCanId, value);
  }
}

}  // namespace esp32m
