#include <esp_log.h>

#include "esp32m/bus/twai.hpp"

namespace esp32m {

  TWAI::TWAI(twai_node_handle_t handle, RxCallback rxCallback,
             int poolDepth)
      : _handle(handle),
        _rxCallback(std::move(rxCallback)),
        _poolDepth(poolDepth) {
    if (!_rxCallback)
      return;

    _pool = new PoolSlot[_poolDepth];
    for (int i = 0; i < _poolDepth; i++) {
      _pool[i].frame.buffer = _pool[i].data;
      _pool[i].frame.buffer_len = sizeof(_pool[i].data);
    }

    _freeSem = xSemaphoreCreateCounting(_poolDepth, _poolDepth);
    _readySem = xSemaphoreCreateCounting(_poolDepth, 0);

    twai_event_callbacks_t cbs = {};
    cbs.on_rx_done = rxIsr;
    cbs.on_error = errorIsr;
    cbs.on_state_change = stateIsr;
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        twai_node_register_event_callbacks(_handle, &cbs, this));

    xTaskCreate([](void* self) { static_cast<TWAI*>(self)->run(); },
                "m/twai/rx", 3072, this, 5, &_task);
  }

  TWAI::~TWAI() {
    // Delete the node first so no further ISR callbacks can fire before we
    // tear down the task and free the pool.
    if (_handle) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(twai_node_delete(_handle));
      _handle = nullptr;
    }
    if (_task) {
      vTaskDelete(_task);
      _task = nullptr;
    }
    if (_readySem) {
      vSemaphoreDelete(_readySem);
      _readySem = nullptr;
    }
    if (_freeSem) {
      vSemaphoreDelete(_freeSem);
      _freeSem = nullptr;
    }
    delete[] _pool;
    _pool = nullptr;
  }

  // ISR: called by the TWAI driver when a frame has been received.
  // Runs in interrupt context - must be IRAM-resident and only use ISR-safe
  // FreeRTOS primitives.
  bool IRAM_ATTR TWAI::rxIsr(twai_node_handle_t handle,
                              const twai_rx_done_event_data_t* /*edata*/,
                              void* ctx) {
    BaseType_t woken = pdFALSE;
    auto* self = static_cast<TWAI*>(ctx);

    // Claim a free slot; if the pool is exhausted the frame is dropped.
    if (xSemaphoreTakeFromISR(self->_freeSem, &woken) != pdTRUE)
      return woken == pdTRUE;

    PoolSlot& slot = self->_pool[self->_writeIdx];
    if (twai_node_receive_from_isr(handle, &slot.frame) == ESP_OK) {
      self->_writeIdx = (self->_writeIdx + 1) % self->_poolDepth;
      xSemaphoreGiveFromISR(self->_readySem, &woken);
    } else {
      // Receive failed; return the slot to the free pool.
      xSemaphoreGiveFromISR(self->_freeSem, &woken);
    }

    return woken == pdTRUE;
  }

  // ISR: called on bus error events.
  // ESP_EARLY_LOG* must be used here - regular logX macros are not ISR-safe.
  bool IRAM_ATTR TWAI::errorIsr(twai_node_handle_t /*handle*/,
                                 const twai_error_event_data_t* edata,
                                 void* /*ctx*/) {
    ESP_EARLY_LOGW("twai", "bus error: 0x%x", (unsigned)edata->err_flags.val);
    return false;
  }

  // ISR: called on node state transitions (error_active/warning/passive/bus_off).
  bool IRAM_ATTR TWAI::stateIsr(twai_node_handle_t /*handle*/,
                                 const twai_state_change_event_data_t* edata,
                                 void* /*ctx*/) {
    static const char* const kStateNames[] = {"error_active", "error_warning",
                                              "error_passive", "bus_off"};
    ESP_EARLY_LOGI("twai", "state: %s -> %s", kStateNames[edata->old_sta],
                   kStateNames[edata->new_sta]);
    return false;
  }

  // Task: dequeues frames from the pool and dispatches them to _rxCallback.
  void TWAI::run() {
    for (;;) {
      if (xSemaphoreTake(_readySem, portMAX_DELAY) != pdTRUE)
        continue;
      _rxCallback(&_pool[_readIdx].frame);
      _readIdx = (_readIdx + 1) % _poolDepth;
      xSemaphoreGive(_freeSem);
    }
  }

}  // namespace esp32m
