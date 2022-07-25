#include "esp32m/io/rmt.hpp"
#include "esp32m/dev/opentherm.hpp"

namespace esp32m {
  namespace opentherm {
    const int RmtResolutonHz = 1000000;

    void encodeBit(rmt_symbol_word_t &item, bool bit) {
      item.duration0 = 500;
      item.duration1 = 500;
      if (bit) {
        item.level0 = 0;
        item.level1 = 1;
      } else {
        item.level0 = 1;
        item.level1 = 0;
      }
    }

    class Driver : public IDriver {
     public:
      Driver(gpio_num_t rx, gpio_num_t tx) : _rxPin(rx), _txPin(tx) {
        _rx = new io::RmtRx(FrameSizeBits*2);
        _tx = new io::RmtTx();
      }
      ~Driver() override {
        delete _rx;
        delete _tx;
      }
      esp_err_t send(uint32_t frame) override {
        ESP_CHECK_RETURN(ensureReady());
        rmt_symbol_word_t tx_items[FrameSizeBits] = {};
        int i = 0;
        encodeBit(tx_items[i++], true);
        while (i <= 32) {
          encodeBit(tx_items[i], (frame & ((uint32_t)1 << (32 - i))) != 0);
          i++;
        }
        encodeBit(tx_items[i], true);
        ESP_CHECK_RETURN(_tx->transmit(tx_items, FrameSizeBits));
        ESP_CHECK_RETURN(_tx->wait(-1));
        return ESP_OK;
      }

      esp_err_t receive(Recv &recv) override {
        ESP_CHECK_RETURN(ensureReady());
        ESP_CHECK_RETURN(_rx->beginReceive());
        rmt_rx_done_event_data_t data;
        ESP_CHECK_RETURN(_rx->endReceive(data, 1150));
        size_t length = data.num_symbols;
        if (length) {
          for (int i = 0; i < length; i++) {
            auto item = data.received_symbols[i];
            if (item.duration0 > 400)
              recv.consume(item.level0);
            if (item.duration0 > 750)
              recv.consume(item.level0);
            if (item.duration1 == 0 ||
                item.duration1 > 400)  // allow last zero to be short
              recv.consume(item.level1);
            if (item.duration1 > 750)
              recv.consume(item.level1);
          }
          recv.finalize();
        } else
          recv.state = RecvState::Timeout;
        return ESP_OK;
      }

     private:
      gpio_num_t _rxPin;
      gpio_num_t _txPin;
      io::RmtRx *_rx;
      io::RmtTx *_tx;
      bool _ready = false;
      esp_err_t ensureReady() {
        if (!_ready) {
          rmt_rx_channel_config_t rxcfg = {
            .gpio_num = _rxPin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = RmtResolutonHz,  // in us
#if SOC_RMT_SUPPORT_RX_PINGPONG
            .mem_block_symbols = 64,  // when the chip is ping-pong capable,
                                      // we can use less rx memory blocks
#else
            .mem_block_symbols = FrameSizeBits,
#endif
            .flags = {}
          };
          ESP_CHECK_RETURN(_rx->setConfig(rxcfg));
          ESP_CHECK_RETURN(_rx->setSignalThresholds(400 * 1000, 1150 * 1000));
          rmt_tx_channel_config_t txcfg = {
              .gpio_num = _txPin,
              .clk_src = RMT_CLK_SRC_DEFAULT,
              .resolution_hz = RmtResolutonHz,  // in us
              .mem_block_symbols = 64,  // ping-pong is always avaliable on tx
                                        // channel, save hardware memory blocks
              .trans_queue_depth = 4,
              .flags = {.invert_out = false,
                        .with_dma = false,
                        .io_loop_back = false,
                        .io_od_mode = false}};
          ESP_CHECK_RETURN(_tx->setConfig(txcfg));
          const static rmt_transmit_config_t txconfig = {
              .loop_count = 0,  // no transfer loop
              .flags = {.eot_level = 1}};
          ESP_CHECK_RETURN(_tx->setTxConfig(txconfig));

          ESP_CHECK_RETURN(_rx->enable());
          ESP_CHECK_RETURN(_tx->enable());
          _ready = true;
        }
        return ESP_OK;
      }
    };

    IDriver *newRmtDriver(gpio_num_t rx, gpio_num_t tx) {
      return new Driver(rx, tx);
    }
  }  // namespace opentherm
}  // namespace esp32m