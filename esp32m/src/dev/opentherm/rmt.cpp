#include "esp32m/io/rmt.hpp"
#include "esp32m/dev/opentherm.hpp"
#include "esp32m/logging.hpp"
#include <cinttypes>

namespace esp32m {
  namespace opentherm {
    const int RmtResolutonHz = 1000000;

    void encodeBit(rmt_symbol_word_t& item, bool bit) {
      item.duration0 = 500;
      item.duration1 = 500;
      // TX hardware INVERTS the signal: GPIO HIGH (1) = bus IDLE,
      //                                  GPIO LOW  (0) = bus ACTIVE.
      // (PC817B LED is driven via R46; when GPIO LOW the LED is lit,
      //  optocoupler ON → Q7 (PNP) ON → master sinks current → bus ACTIVE.)
      // OT bit "1" per spec = active first half, idle second half:
      //   level0 = 0 (GPIO LOW = bus active), level1 = 1 (GPIO HIGH = idle).
      // OT bit "0" = idle first, active second:
      //   level0 = 1, level1 = 0.
      // After the stop bit (which ends on level1=1), eot_level=1 keeps
      // GPIO HIGH = bus IDLE so the boiler can drive the bus in reply.
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
        return ESP_OK;
      }

      esp_err_t receive(Recv& recv) override {
        ESP_CHECK_RETURN(ensureReady());
        rmt_rx_done_event_data_t data;
        // Arm RX after TX so the TX leakage (optocoupler crosstalk on the
        // board) does not corrupt the receive buffer.  The OT spec requires the
        // slave to wait at least 20 ms before responding, which gives ample
        // time to arm the channel before any reply arrives.
        ESP_CHECK_RETURN(_rx->enable());
        const int TotalTimeoutMs = 800 + 1150 * FrameSizeBits / 1000;
        const TickType_t deadline =
            xTaskGetTickCount() + pdMS_TO_TICKS(TotalTimeoutMs);
        esp_err_t err;
        for (;;) {
          err = _rx->beginReceive();
          if (err != ESP_OK)
            break;
          int remainingMs =
              (int)((int32_t)(deadline - xTaskGetTickCount()) *
                    portTICK_PERIOD_MS);
          if (remainingMs <= 0) {
            err = ESP_ERR_TIMEOUT;
            break;
          }
          err = _rx->endReceive(data, remainingMs);
          if (err != ESP_OK)
            break;
          if (data.num_symbols > 0)
            break;  // got a real frame
          // 0 symbols: 60ms signal_range_max idle gap fired; re-arm
        }
        // never leave receiver enabled, because it will collect phantom pulses
        // on rx line when we are transmitting
        ESP_CHECK_RETURN(_rx->disable());
        ESP_CHECK_RETURN(err);
        size_t length = data.num_symbols;
        if (length) {
          for (int i = 0; i < length; i++) {
            auto item = data.received_symbols[i];
            // RX hardware: bus LOW (active)  → PC817B OFF → GPIO HIGH (1).
            //              bus HIGH (idle)   → PC817B ON  → GPIO LOW  (0).
            // Boiler bit "1" = active first (GPIO 1), idle second (GPIO 0).
            // consume(1) then consume(0) → mbuf=0b10=ManchesterOne ✓
            // No inversion needed: raw GPIO level maps directly to what the
            // Manchester decoder expects.
            //
            // Half-bit count per RMT symbol is determined by rounding the
            // TOTAL symbol duration (duration0 + duration1) to the nearest
            // 500µs multiple to get ntotal, then distributing between the
            // two levels.  n0 is independently estimated from duration0,
            // then clamped to [ntotal-2, ntotal-1] (each further clamped to
            // [1, 2]).  This two-sided clamp ensures n1 = ntotal-n0 ∈ [1, 2]
            // without ever inflating ntotal to accommodate an overestimated n0.
            //
            // Critical case: (757µs, 257µs) total=1014µs → ntotal=2.
            //   n0 formula = round(757/500) = 2.  Upper clamp: n0 ≤ ntotal-1=1
            //   → n0=1, n1=1. Correct (2 hb).
            //   Old code had `if (ntotal < n0+1) ntotal = n0+1` which forced
            //   ntotal=3 → 3 hb, breaking the decoder.
            //
            //   (500,500)   → 2 hb (1+1)
            //   (1000,500)  → 3 hb (2+1)
            //   (500,1000)  → 3 hb (1+2)
            //   (1000,1000) → 4 hb (2+2)
            //   (757,257)   → 2 hb (1+1) ← skewed duty, was wrongly 3
            //   (1250,750)  → 4 hb (2+2) ← wide merged pair
            //   (64,450)    → 1 hb (0+1) ← glitch spike
            bool last = (item.duration1 == 0);
            bool glitch0 = (item.duration0 < 150);
            int n0 = glitch0 ? 0 : (((int)item.duration0 + 250) / 500);
            if (n0 > 2) n0 = 2;
            int n1;
            if (last) {
              // duration1=0 marks end-of-data; always consume level1 once.
              n1 = 1;
            } else {
              int ntotal = glitch0
                  ? (((int)item.duration1 + 250) / 500)
                  : (((int)(item.duration0 + item.duration1) + 250) / 500);
              if (ntotal < 2) ntotal = 2;
              if (ntotal > 4) ntotal = 4;
              if (!glitch0) {
                // Clamp n0 to [ntotal-2, ntotal-1] so n1=ntotal-n0 ∈ [1,2].
                int n0_lo = ntotal - 2; if (n0_lo < 1) n0_lo = 1;
                int n0_hi = ntotal - 1; if (n0_hi > 2) n0_hi = 2;
                if (n0 < n0_lo) n0 = n0_lo;
                if (n0 > n0_hi) n0 = n0_hi;
              }
              n1 = ntotal - n0;
            }
            for (int k = 0; k < n0; k++) recv.consume(item.level0);
            for (int k = 0; k < n1; k++) recv.consume(item.level1);
          }
          recv.finalize();
          if (recv.state != RecvState::MessageValid) {
            logw("ot bad frame (state=%d, buf=0x%" PRIx32 ", pos=%d)",
                 (int)recv.state, recv.buf, recv.pos);
            io::rmt::dump("ot bad frame", data.received_symbols,
                          data.num_symbols);
          }
        } else
          recv.state = RecvState::Timeout;
        return ESP_OK;
      }

     private:
      gpio_num_t _rxPin;
      gpio_num_t _txPin;
      io::RmtRx* _rx;
      io::RmtTx* _tx;
      bool _ready = false;
      esp_err_t ensureReady() {
        if (!_ready) {
          rmt_rx_channel_config_t rxcfg = {};
          rxcfg.gpio_num = _rxPin;
          rxcfg.clk_src = RMT_CLK_SRC_DEFAULT;
          rxcfg.resolution_hz = RmtResolutonHz;  // in us
          // Worst-case OT frame is 34 bits × 2 RMT symbols = 68 symbols.
          // Worst-case OT frame is 34 bits × 2 RMT symbols = 68 symbols.
          // mem_block_symbols must be ≥ 68 so the hardware never silently drops
          // the tail of a frame before the ISR fires.  128 (2 × 64-word blocks)
          // gives headroom and is supported on plain ESP32 without DMA.
          rxcfg.mem_block_symbols = 128;
          ESP_CHECK_RETURN(_rx->setConfig(rxcfg));
          // signal_range_min_ns: glitch filter – pulses shorter than this are
          // discarded in hardware.  The filter register uses the 80 MHz APB
          // clock (8-bit, max ~3187 ns); 2000 ns rejects sub-µs spikes safely.
          // signal_range_max_ns: end-of-frame idle detector – the RMT stops
          // capturing when it sees a gap longer than this.  The hardware limit
          // at 1 MHz resolution is 65535 µs; we use 60 ms which is comfortably
          // above the max valid OT pulse (~1300 µs) and below the limit.
          // The master→slave idle gap can be 20–800 ms, longer than this value,
          // so receive() re-arms the hardware in a loop until actual frame
          // symbols arrive or the overall deadline expires.
          ESP_CHECK_RETURN(_rx->setSignalThresholds(2000, 60 * 1000 * 1000));
          rmt_tx_channel_config_t txcfg = {};
          txcfg.gpio_num = _txPin;
          txcfg.clk_src = RMT_CLK_SRC_DEFAULT;
          txcfg.resolution_hz = RmtResolutonHz;  // in us
          txcfg.mem_block_symbols = 64;
          txcfg.trans_queue_depth = 4;
          ESP_CHECK_RETURN(_tx->setConfig(txcfg));
          rmt_transmit_config_t txconfig = {};
          txconfig.loop_count = 0;  // no transfer loop
          // eot_level=1: after the stop bit the TX GPIO stays HIGH = bus IDLE.
          // (GPIO HIGH = bus IDLE per hardware inversion — see encodeBit above.)
          // eot_level=0 would force GPIO LOW = bus ACTIVE immediately after the
          // frame, preventing the boiler from replying.
          txconfig.flags.eot_level = 1;
          ESP_CHECK_RETURN(_tx->setTxConfig(txconfig));

          ESP_CHECK_RETURN(_tx->enable());

          // Drive the bus with a single idle bit at startup so the TX GPIO
          // settles at bus IDLE (eot_level=1 = GPIO HIGH) before the first
          // real frame. The 1 s delay also gives the boiler time to initialize.
          // Note: GPIO LOW (0) = bus ACTIVE, GPIO HIGH (1) = bus IDLE.
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

    IDriver* newRmtDriver(gpio_num_t rx, gpio_num_t tx) {
      return new Driver(rx, tx);
    }
  }  // namespace opentherm
}  // namespace esp32m