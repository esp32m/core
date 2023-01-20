#include "esp32m/io/rmt.hpp"
#include "esp32m/dev/opentherm.hpp"
#include "esp32m/logging.hpp"

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
        _rx = new io::RmtRx(FrameSizeBits * 2);
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
        ESP_CHECK_RETURN(_tx->wait());
        // logd("ot sent frame %x", frame);
        return ESP_OK;
      }

      esp_err_t receive(Recv &recv) override {
        ESP_CHECK_RETURN(ensureReady());
        rmt_rx_done_event_data_t data;
        ESP_CHECK_RETURN(_rx->enable());
        ESP_CHECK_RETURN(_rx->beginReceive());
        auto err = _rx->endReceive(
            data, 800 /* slave must respond within 800ms max )*/ +
                      1150 * FrameSizeBits /
                          1000 /* max 1150us between bit transition */);
        // never leave receiver enabled, because it will collect phantom pulses
        // on rx line when we are transmitting
        ESP_CHECK_RETURN(_rx->disable());
        ESP_CHECK_RETURN(err);
        // io::rmt::dump("ot recv frame", data.received_symbols,
        // data.num_symbols);
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
          /*if (recv.state != RecvState::MessageValid)
            io::rmt::dump("ot bad frame", data.received_symbols,
                          data.num_symbols);*/
        } else
          recv.state = RecvState::Timeout;
        // logd("ot recv frame %x state %d", recv.buf, (int)recv.state);
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
              .mem_block_symbols = 64,  // we can't go lower, even though we
                                        // need only FrameSizeBits
              .flags = {}};
          ESP_CHECK_RETURN(_rx->setConfig(rxcfg));
          // be a little more permissive, especially for the max pulse duration,
          // I've seen it over 1.2ms
          ESP_CHECK_RETURN(
              _rx->setSignalThresholds((400 - 50) * 1000, (1150 + 150) * 1000));
          rmt_tx_channel_config_t txcfg = {
              .gpio_num = _txPin,
              .clk_src = RMT_CLK_SRC_DEFAULT,
              .resolution_hz = RmtResolutonHz,  // in us
              .mem_block_symbols = 64,
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

          ESP_CHECK_RETURN(_tx->enable());

          // RX pin is low initially, make it high
          // remember that our hardware inverts TX signal, so 1 is IDLE
          // RX pin is not inverted, IDLE is 0
          rmt_symbol_word_t tx_items[1];
          encodeBit(tx_items[0], true);
          ESP_CHECK_RETURN(_tx->transmit(tx_items, 1));
          ESP_CHECK_RETURN(_tx->wait());
          delay(1000);
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