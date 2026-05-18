#pragma once

#include <esp_twai.h>
#include <esp_twai_onchip.h>

#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "esp32m/logging.hpp"

namespace esp32m {

  class TWAI {
   public:
    using RxCallback = std::function<void(twai_frame_t*)>;

    // If rxCallback is non-null the RX pipeline is set up: native event
    // callbacks are registered, a pool of poolDepth frame slots is allocated,
    // and a dedicated task is spawned to dispatch received frames to rxCallback.
    // If rxCallback is null none of that happens (TX-only or externally managed
    // receive).
    TWAI(twai_node_handle_t handle, RxCallback rxCallback = nullptr,
         int poolDepth = 32);
    virtual ~TWAI();

    esp_err_t enable(bool en) {
      return en ? twai_node_enable(_handle) : twai_node_disable(_handle);
    }
    esp_err_t recover() {
      return twai_node_recover(_handle);
    }
    esp_err_t setRangeFilter(uint8_t filter_id,
                             const twai_range_filter_config_t* range_cfg) {
      return twai_node_config_range_filter(_handle, filter_id, range_cfg);
    }
    esp_err_t transmit(const twai_frame_t* frame, int timeout_ms) {
      return twai_node_transmit(_handle, frame, timeout_ms);
    }
    esp_err_t waitTxDone(int timeout_ms) {
      return twai_node_transmit_wait_all_done(_handle, timeout_ms);
    }
    esp_err_t info(twai_node_status_t* status_ret,
                   twai_node_record_t* statistics_ret) {
      return twai_node_get_info(_handle, status_ret, statistics_ret);
    }

   protected:
    twai_node_handle_t _handle;
    RxCallback _rxCallback;

   private:
    // One pool slot holds a twai_frame_t together with its inline data buffer
    // so that the ISR can call twai_node_receive_from_isr() without any
    // additional allocation.
    struct PoolSlot {
      twai_frame_t frame;
      uint8_t data[TWAI_FRAME_MAX_LEN];
    };

    int _poolDepth = 0;
    PoolSlot* _pool = nullptr;
    SemaphoreHandle_t _freeSem = nullptr;   // counts free (writable) slots
    SemaphoreHandle_t _readySem = nullptr;  // counts ready (readable) slots
    TaskHandle_t _task = nullptr;
    int _writeIdx = 0;
    int _readIdx = 0;

    // ISR-context callbacks registered with twai_node_register_event_callbacks
    static bool IRAM_ATTR rxIsr(twai_node_handle_t handle,
                                const twai_rx_done_event_data_t* edata,
                                void* ctx);
    static bool IRAM_ATTR errorIsr(twai_node_handle_t handle,
                                   const twai_error_event_data_t* edata,
                                   void* ctx);
    static bool IRAM_ATTR stateIsr(twai_node_handle_t handle,
                                   const twai_state_change_event_data_t* edata,
                                   void* ctx);

    void run();
  };

}  // namespace esp32m