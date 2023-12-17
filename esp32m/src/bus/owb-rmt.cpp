#include "esp32m/bus/owb.hpp"
#include "esp32m/io/rmt.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {

  namespace owb {

    const unsigned int DurationStart = 2;
    const unsigned int DurationBit = 60;
    const unsigned int DurationRecovery = 2;

    const static rmt_symbol_word_t sbit0 = {
        .duration0 = 60,  // DurationStart + DurationBit,
        .level0 = 0,
        .duration1 = 10,  // DurationRecovery,
        .level1 = 1};

    const static rmt_symbol_word_t sbit1 = {
        .duration0 = 6,  // DurationStart,
        .level0 = 0,
        .duration1 = 64,  // DurationBit + DurationRecovery,
        .level1 = 1};

    const unsigned int DurationResetPulse = 480;

    const static rmt_symbol_word_t sreset = {.duration0 = DurationResetPulse,
                                             .level0 = 0,
                                             .duration1 = 0,
                                             .level1 = 1};

    const int RmtResolutonHz = 1000000;
    const unsigned int DurationResetPresenceWaitMin = 15;
    const unsigned int DurationResetPresenceMin = 60;

    bool checkPresencePulse(rmt_symbol_word_t *rmt_symbols, size_t symbol_num) {
      // dump(rmt_symbols, symbol_num);
      if (symbol_num >= 2) {
        // there should be at least 2 symbols(3 or 4 edges)
        if (rmt_symbols[0].level1 == 1) {  // bus is high before reset pulse
          if (rmt_symbols[0].duration1 > DurationResetPresenceWaitMin &&
              rmt_symbols[1].duration0 > DurationResetPresenceMin) {
            return true;
          }
        } else {  // bus is low before reset pulse(first pulse after rmt channel
                  // init)
          if (rmt_symbols[0].duration0 > DurationResetPresenceWaitMin &&
              rmt_symbols[1].duration1 > DurationResetPresenceMin) {
            return true;
          }
        }
      }
      return false;
    }

    const unsigned int DurationBitSample = 15 - 2;
    void decode(rmt_symbol_word_t *rmt_symbols, size_t symbol_num,
                uint8_t *decoded_bytes) {
      size_t byte_pos = 0, bit_pos = 0;
      for (size_t i = 0; i < symbol_num; i++) {
        if (rmt_symbols[i].level0 == 0 && rmt_symbols[i].level1 == 1 &&
            rmt_symbols[i].duration0 < DurationBitSample)
          decoded_bytes[byte_pos] |= 1 << bit_pos;
        bit_pos++;
        if (bit_pos >= 8) {
          bit_pos = 0;
          byte_pos++;
        }
      }
    }

    class RmtDriver : public IDriver {
     public:
      RmtDriver(gpio_num_t pin, size_t maxRxBytes)
          : IDriver(pin), _maxRxBytes(maxRxBytes) {
        _rx = new io::RmtRx((maxRxBytes + 2) * 8);
        _tx = new io::RmtTx();
      }
      ~RmtDriver() {
        // delete _txBytes;
        delete _tx;
        delete _rx;
      }
      esp_err_t reset(bool &present) override {
        present = false;
        ESP_CHECK_RETURN(ensureReady());
        ESP_CHECK_RETURN(_rx->setSignalThresholds(
            3 * 1000, (DurationResetPulse + 10) * 1000));
        ESP_CHECK_RETURN(_rx->beginReceive());
        ESP_CHECK_RETURN(_tx->transmit(&sreset, 1));
        rmt_rx_done_event_data_t data;
        ESP_CHECK_RETURN(_rx->endReceive(data));
        present = checkPresencePulse(data.received_symbols, data.num_symbols);
        return ESP_OK;
      }
      esp_err_t readBits(uint8_t *in, int number_of_bits_to_read) override {
        if (number_of_bits_to_read > 8)
          return ESP_FAIL;
        ESP_CHECK_RETURN(ensureReady());
        ESP_CHECK_RETURN(
            _rx->setSignalThresholds(1 * 1000, (DurationBit + 10) * 1000));
        ESP_CHECK_RETURN(_rx->beginReceive());
        rmt_symbol_word_t syms[number_of_bits_to_read];
        for (int i = 0; i < number_of_bits_to_read; i++) syms[i] = sbit1;
        ESP_CHECK_RETURN(_tx->transmit(syms, number_of_bits_to_read));
        rmt_rx_done_event_data_t data;
        ESP_CHECK_RETURN(_rx->endReceive(data));
        uint8_t byte = 0;
        decode(data.received_symbols, data.num_symbols, &byte);
        *in = byte & (0xff >> (8 - number_of_bits_to_read));
        return ESP_OK;
      }
      esp_err_t writeBits(uint8_t out, int number_of_bits_to_write) override {
        if (number_of_bits_to_write > 8)
          return ESP_FAIL;
        ESP_CHECK_RETURN(ensureReady());
        rmt_symbol_word_t syms[number_of_bits_to_write];
        for (int i = 0; i < number_of_bits_to_write; i++) {
          syms[i] = (out & 0x01) ? sbit1 : sbit0;
          out >>= 1;
        }
        ESP_CHECK_RETURN(_tx->transmit(syms, number_of_bits_to_write));
        ESP_CHECK_RETURN(_tx->wait());
        return ESP_OK;
      }

     private:
      io::RmtRx *_rx;
      io::RmtTx *_tx;
      size_t _maxRxBytes;
      bool _ready = false;
      esp_err_t ensureReady() {
        if (!_ready) {
          rmt_rx_channel_config_t rxcfg = {};
          rxcfg.gpio_num = pin();
          rxcfg.clk_src = RMT_CLK_SRC_DEFAULT;
          rxcfg.resolution_hz = RmtResolutonHz;  // in us
#if SOC_RMT_SUPPORT_RX_PINGPONG
          rxcfg.mem_block_symbols = 64;  // when the chip is ping-pong capable,
                                         // we can use less rx memory blocks
#else
          rxcfg.mem_block_symbols = (_maxRxBytes + 2) * 8;
#endif
          ESP_CHECK_RETURN(_rx->setConfig(rxcfg));
          rmt_tx_channel_config_t txcfg = {};
          txcfg.gpio_num = pin();
          txcfg.clk_src = RMT_CLK_SRC_DEFAULT;
          txcfg.resolution_hz = RmtResolutonHz;  // in us
          txcfg.mem_block_symbols = 64;  // ping-pong is always avaliable on tx
                                         // channel, save hardware memory blocks
          txcfg.trans_queue_depth = 4;
          // make tx channel coexist with rx channel on the same gpio pin
          txcfg.flags.io_loop_back = true;
          // enable open-drain mode for 1-wire bus
          txcfg.flags.io_od_mode = true;

          ESP_CHECK_RETURN(_tx->setConfig(txcfg));
          rmt_transmit_config_t txconfig = {};
          txconfig.loop_count = 0;  // no transfer loop
          // onewire bus should be released in IDLE
          txconfig.flags.eot_level = 1;
          ESP_CHECK_RETURN(_tx->setTxConfig(txconfig));

          ESP_CHECK_RETURN(_rx->enable());
          ESP_CHECK_RETURN(_tx->enable());
          _ready = true;
        }
        return ESP_OK;
      }
    };

    IDriver *newRmtDriver(gpio_num_t pin) {
      return new RmtDriver(pin, 10);
    }

  }  // namespace owb
}  // namespace esp32m