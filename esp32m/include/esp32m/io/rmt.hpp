#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_types.h"

namespace esp32m {
  namespace io {
    class Rmt {
     public:
      Rmt(const Rmt &) = delete;
      virtual ~Rmt();
      esp_err_t enable();
      esp_err_t disable();
      esp_err_t applyCarrier(const rmt_carrier_config_t *carrier);

     protected:
      rmt_channel_handle_t _handle = nullptr;
      bool _enabled = false;
      Rmt() {}
      virtual esp_err_t ensureInited() = 0;
    };
    class RmtRx : public Rmt {
     public:
      RmtRx(size_t bufSize);
      ~RmtRx() override;
      esp_err_t setConfig(const rmt_rx_channel_config_t &config);
      esp_err_t setSignalThresholds(uint32_t nsMin, uint32_t nsMax);
      esp_err_t beginReceive();
      esp_err_t endReceive(rmt_rx_done_event_data_t &data,
                           int timeoutMs = portMAX_DELAY);

     protected:
      esp_err_t ensureInited() override;

     private:
      rmt_rx_channel_config_t _config = {};
      rmt_receive_config_t _thresholds = {.signal_range_min_ns = 1000,
                                          .signal_range_max_ns = 1000 * 1000};
      QueueHandle_t _queue;
      void *_buf;
      size_t _bufsize;

      bool recvDone(const rmt_rx_done_event_data_t *edata);
    };
    class RmtTx : public Rmt {
     public:
      RmtTx();
      ~RmtTx() override;
      esp_err_t setConfig(const rmt_tx_channel_config_t &config);
      esp_err_t setTxConfig(const rmt_transmit_config_t &config);
      esp_err_t transmit(const rmt_symbol_word_t *data, size_t count);
      esp_err_t transmit(rmt_encoder_handle_t encoder, const void *data,
                         size_t bytes);
      esp_err_t wait(int ms = portMAX_DELAY);

     protected:
      esp_err_t ensureInited() override;

     private:
      rmt_tx_channel_config_t _config = {};
      rmt_transmit_config_t _txconfig = {};
      rmt_encoder_handle_t _copyEncoder = nullptr;
    };

    class RmtByteEncoder {
     public:
      RmtByteEncoder(RmtTx *tx, const rmt_bytes_encoder_config_t &config)
          : _tx(tx), _config(config) {}
      esp_err_t transmit(const uint8_t *bytes, size_t count);

     private:
      RmtTx *_tx;
      rmt_bytes_encoder_config_t _config;
      rmt_encoder_handle_t _handle = nullptr;
    };

    namespace rmt {

      void dump(const char *msg, rmt_symbol_word_t *syms,
                size_t count);
    }  // namespace rmt

  }  // namespace io
}  // namespace esp32m