#include "esp32m/io/rmt.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

#include <cstring>

namespace esp32m {
  namespace io {

    Rmt::~Rmt() {
      if (!_handle)
        return;
      if (_enabled)
        disable();
      ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_del_channel(_handle));
      _handle = nullptr;
    }
    esp_err_t Rmt::enable() {
      ESP_CHECK_RETURN(ensureInited());
      ESP_CHECK_RETURN(rmt_enable(_handle));
      _enabled = true;
      return ESP_OK;
    }
    esp_err_t Rmt::disable() {
      ESP_CHECK_RETURN(ensureInited());
      ESP_CHECK_RETURN(rmt_disable(_handle));
      _enabled = false;
      return ESP_OK;
    }
    esp_err_t Rmt::applyCarrier(const rmt_carrier_config_t* config) {
      ESP_CHECK_RETURN(ensureInited());
      ESP_CHECK_RETURN(rmt_apply_carrier(_handle, config));
      return ESP_OK;
    }

    RmtRx::RmtRx(size_t bufsize) {
      _bufsize = bufsize * sizeof(rmt_symbol_word_t);
      _buf = malloc(_bufsize);
      _queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    }
    RmtRx::~RmtRx() {
      if (_queue)
        vQueueDelete(_queue);
      if (_buf)
        free(_buf);
    }
    esp_err_t RmtRx::setConfig(const rmt_rx_channel_config_t& config) {
      if (memcmp(&_config, &config, sizeof(rmt_rx_channel_config_t)) == 0)
        return ESP_OK;
      _config = config;
      if (_handle) {
        ESP_CHECK_RETURN(rmt_del_channel(_handle));
        _handle = nullptr;
        ESP_CHECK_RETURN(ensureInited());
      }
      return ESP_OK;
    }
    esp_err_t RmtRx::setSignalThresholds(uint32_t nsMin, uint32_t nsMax) {
      _thresholds.signal_range_min_ns = nsMin;
      _thresholds.signal_range_max_ns = nsMax;
      return ESP_OK;
    }
    esp_err_t RmtRx::beginReceive() {
      ESP_CHECK_RETURN(ensureInited());
      ESP_CHECK_RETURN(rmt_receive(_handle, _buf, _bufsize, &_thresholds));
      return ESP_OK;
    }
    esp_err_t RmtRx::endReceive(rmt_rx_done_event_data_t& data, int timeoutMs) {
      if (xQueueReceive(_queue, &data, pdMS_TO_TICKS(timeoutMs)) == pdPASS)
        return ESP_OK;
      return ESP_FAIL;
    }

    esp_err_t RmtRx::ensureInited() {
      if (!_handle) {
        ESP_CHECK_RETURN(rmt_new_rx_channel(&_config, &_handle));
        rmt_rx_event_callbacks_t cbs = {
            .on_recv_done = [](rmt_channel_handle_t rx_chan,
                               rmt_rx_done_event_data_t* edata,
                               void* user_ctx) {
              return ((RmtRx*)user_ctx)->recvDone(edata);
            }};
        ESP_CHECK_RETURN(rmt_rx_register_event_callbacks(_handle, &cbs, this));
        if (_enabled) {
          _enabled = false;
          ESP_CHECK_RETURN(enable());
        }
      }
      return ESP_OK;
    }
    bool RmtRx::recvDone(rmt_rx_done_event_data_t* edata) {
      BaseType_t task_woken = pdFALSE;
      xQueueSendFromISR(_queue, edata, &task_woken);
      return task_woken;
    }

    RmtTx::RmtTx() {}
    RmtTx::~RmtTx() {
      if (_copyEncoder)
        rmt_del_encoder(_copyEncoder);
    };

    esp_err_t RmtTx::setConfig(const rmt_tx_channel_config_t& config) {
      if (memcmp(&_config, &config, sizeof(rmt_tx_channel_config_t)) == 0)
        return ESP_OK;
      _config = config;
      if (_handle) {
        ESP_CHECK_RETURN(rmt_del_channel(_handle));
        _handle = nullptr;
        ESP_CHECK_RETURN(ensureInited());
      }
      return ESP_OK;
    }
    esp_err_t RmtTx::setTxConfig(const rmt_transmit_config_t& config) {
      _txconfig = config;
      return ESP_OK;
    }
    esp_err_t RmtTx::transmit(const rmt_symbol_word_t* data, size_t count) {
      ESP_CHECK_RETURN(ensureInited());
      if (!_copyEncoder) {
        rmt_copy_encoder_config_t cfg = {};
        ESP_CHECK_RETURN(rmt_new_copy_encoder(&cfg, &_copyEncoder));
      }
      ESP_CHECK_RETURN(rmt_transmit(_handle, _copyEncoder, data,
                                    count * sizeof(rmt_symbol_word_t),
                                    &_txconfig));
      return ESP_OK;
    }
    esp_err_t RmtTx::transmit(rmt_encoder_handle_t encoder, const void* data,
                              size_t bytes) {
      ESP_CHECK_RETURN(ensureInited());
      ESP_CHECK_RETURN(rmt_transmit(_handle, encoder, data, bytes, &_txconfig));
      return ESP_OK;
    }
    esp_err_t RmtTx::wait(int ms) {
      ESP_CHECK_RETURN(ensureInited());
      ESP_CHECK_RETURN(rmt_tx_wait_all_done(_handle, ms));
      return ESP_OK;
    }

    esp_err_t RmtTx::ensureInited() {
      if (!_handle) {
        ESP_CHECK_RETURN(rmt_new_tx_channel(&_config, &_handle));
        if (_enabled) {
          _enabled = false;
          ESP_CHECK_RETURN(enable());
        }
      }
      return ESP_OK;
    }

    esp_err_t RmtByteEncoder::transmit(const uint8_t* bytes, size_t count) {
      if (!_handle) {
        ESP_CHECK_RETURN(rmt_new_bytes_encoder(&_config, &_handle));
      }
      return _tx->transmit(_handle, bytes, count);
    }

  }  // namespace io
}  // namespace esp32m