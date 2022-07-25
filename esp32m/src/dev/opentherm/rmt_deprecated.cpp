#ifdef ESP32M_USE_DEPRECATED_RMT

#include <driver/rmt.h>
#include "esp32m/dev/opentherm.hpp"
#include "esp32m/io/rmt_dep.hpp"

namespace esp32m {
  namespace opentherm {

    void encodeBit(rmt_item32_t &item, bool bit) {
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
      Driver(deprecated::RmtRx *rx, deprecated::RmtTx *tx) : _rx(rx), _tx(tx) {}
      ~Driver() override {
        delete _rx;
        delete _tx;
      }
      esp_err_t send(uint32_t frame) override {
        ESP_CHECK_RETURN(ensureReady());
        rmt_item32_t tx_items[FrameSizeBits] = {};
        int i = 0;
        encodeBit(tx_items[i++], true);
        while (i <= 32) {
          encodeBit(tx_items[i], (frame & ((uint32_t)1 << (32 - i))) != 0);
          i++;
        }
        encodeBit(tx_items[i], true);
        ESP_CHECK_RETURN(_tx->write(tx_items, FrameSizeBits, true));
        return ESP_OK;
      }

      esp_err_t receive(Recv &recv) override {
        ESP_CHECK_RETURN(ensureReady());
        if (!_rb)
          return ESP_FAIL;
        size_t length = 0;
        rmt_item32_t *items = NULL;
        ESP_CHECK_RETURN(_rx->start(true));
        items = (rmt_item32_t *)xRingbufferReceive(_rb, &length,
                                                   pdMS_TO_TICKS(1150));
        if (items) {
          length >>= 2;
          for (int i = 0; i < length; i++) {
            rmt_item32_t item = items[i];
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
          vRingbufferReturnItem(_rb, (void *)items);
        } else
          recv.state = RecvState::Timeout;
        ESP_CHECK_RETURN(_rx->stop());
        return ESP_OK;
      }

     private:
      deprecated::RmtRx *_rx;
      deprecated::RmtTx *_tx;
      RingbufHandle_t _rb = nullptr;
      bool _ready = false;
      esp_err_t ensureReady() {
        if (!_ready) {
          ESP_CHECK_RETURN(_tx->disableCarrier());
          ESP_CHECK_RETURN(_tx->enableIdle(RMT_IDLE_LEVEL_HIGH));
          ESP_CHECK_RETURN(_tx->configure());
          ESP_CHECK_RETURN(_tx->install());

          ESP_CHECK_RETURN(_rx->enableFilter(100));
          ESP_CHECK_RETURN(_rx->setIdleThreshold(20000));
          ESP_CHECK_RETURN(_rx->configure());
          ESP_CHECK_RETURN(_rx->install());
          ESP_CHECK_RETURN(_rx->getRingbufHandle(&_rb));
          _ready=true;
        }
        return ESP_OK;
      }
    };

    IDriver *newDeprecatedRmtDriver(gpio_num_t rxPin, gpio_num_t txPin) {
      auto rx = deprecated::useRmtRx(rxPin);
      auto tx = deprecated::useRmtTx(txPin);
      if (rx && tx)
        return new Driver(rx, tx);
      if (rx)
        delete rx;
      if (tx)
        delete tx;
      return nullptr;
    }
  }  // namespace opentherm
}  // namespace esp32m

#endif