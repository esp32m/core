#ifdef ESP32M_USE_DEPRECATED_RMT

#include "esp32m/io/rmt_dep.hpp"
#include "esp32m/bus/owb.hpp"

#include <driver/gpio.h>
#include <driver/rmt.h>
#include <soc/gpio_periph.h>
#include <soc/gpio_struct.h>

namespace esp32m {

  namespace owb {
    // overall slot duration
    const int DurationSlot = 75;
    // write 1 slot and read slot durations [us]
    const int Duration1Low = 2;
    const int Duration1High = DurationSlot - Duration1Low;
    // write 0 slot durations [us]
    const int Duration0Low = 65;
    const int Duration0High = DurationSlot - Duration0Low;

    const int MaxBitsPerSlot = 8;

    class Driver : public IDriver {
     public:
      Driver(gpio_num_t pin, deprecated::RmtRx *rx,
             deprecated::RmtTx *tx)
          : IDriver(pin), _rxCh(rx), _txCh(tx) {
        /*int co = RMT_CHANNEL_0 + index * 2;
        _txCh = (rmt_channel_t)(co + 1);
        _rxCh = (rmt_channel_t)co;*/
        ESP_ERROR_CHECK_WITHOUT_ABORT(init());
      }
      ~Driver() {
        delete _rxCh;
        delete _txCh;
      }
      esp_err_t reset(bool &present) override;
      esp_err_t readBits(uint8_t *in, int number_of_bits_to_read) override;
      esp_err_t writeBits(uint8_t out, int number_of_bits_to_write) override;

     private:
      deprecated::RmtRx *_rxCh;
      deprecated::RmtTx *_txCh;

      bool _useCrc = true;
      bool _useParasiticPower;
      gpio_num_t _pullupGpio = GPIO_NUM_NC;
      RingbufHandle_t _rb = nullptr;

      esp_err_t init();
      void flushRx();
    };

    esp_err_t Driver::init() {
      // RX idle threshold
      // needs to be larger than any duration occurring during write slots
      const int DurationRxIdle = DurationSlot + 2;
      /*rmt_config_t rmt_tx = {};
      rmt_tx.rmt_mode = RMT_MODE_TX;
      rmt_tx.channel = _txCh;
      rmt_tx.gpio_num = _gpio;
      rmt_tx.clk_div = 80;
      rmt_tx.mem_block_num = 1;
      rmt_tx.tx_config.loop_en = false;
      rmt_tx.tx_config.carrier_en = false;
      rmt_tx.tx_config.idle_level = RMT_IDLE_LEVEL_HIGH;
      rmt_tx.tx_config.idle_output_en = true;
      ESP_CHECK_RETURN(rmt_config(&rmt_tx));
ESP_CHECK_RETURN(rmt_driver_install(
          rmt_tx.channel, 0,
          ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_SHARED)); */
      _txCh->disableCarrier();
      _txCh->enableIdle(RMT_IDLE_LEVEL_HIGH);
      ESP_CHECK_RETURN(_txCh->configure());
      ESP_CHECK_RETURN(_txCh->install());
      /*rmt_config_t rmt_rx = {};
      rmt_rx.rmt_mode = RMT_MODE_RX;
      rmt_rx.channel = _rxCh;
      rmt_rx.gpio_num = _gpio;
      rmt_rx.clk_div = 80;
      rmt_rx.mem_block_num = 1;
      rmt_rx.rx_config.filter_en = true;
      rmt_rx.rx_config.filter_ticks_thresh = 30;
      rmt_rx.rx_config.idle_threshold = DurationRxIdle;
      esp_err_t result = ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_config(&rmt_rx));*/
      _rxCh->enableFilter(30);
      _rxCh->setIdleThreshold(DurationRxIdle);
      esp_err_t result = ESP_ERROR_CHECK_WITHOUT_ABORT(_rxCh->configure());
      if (result == ESP_OK) {
        ESP_CHECK_RETURN(_rxCh->install()/*rmt_driver_install(
            rmt_rx.channel, 512,
            ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_SHARED)*/);
        ESP_CHECK_RETURN(_rxCh->getRingbufHandle(&_rb));
      } else
        ESP_CHECK_RETURN(_txCh->uninstall());
      auto gpio = pin();
      // attach GPIO to previous pin
      if (gpio < 32)
        GPIO.enable_w1ts = (0x1 << gpio);
      else
        GPIO.enable1_w1ts.data = (0x1 << (gpio - 32));

      // attach RMT channels to new gpio pin
      // ATTENTION: set pin for rx first since gpio_output_disable() will
      //            remove rmt output signal in matrix!
      ESP_CHECK_RETURN(_rxCh->setGpio(false));
      ESP_CHECK_RETURN(_txCh->setGpio(false));
      /*ESP_CHECK_RETURN(
          rmt_set_gpio(_rxCh->channel(), RMT_MODE_RX, _gpio, false));
      ESP_CHECK_RETURN(
          rmt_set_gpio(_txCh->channel(), RMT_MODE_TX, _gpio, false));*/

      // force pin direction to input to enable path to RX channel
      PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[gpio]);

      // enable open drain
      GPIO.pin[gpio].pad_driver = 1;
      return result;
    }

    esp_err_t Driver::reset(bool &present) {
      if (!_rb)
        return ESP_FAIL;
      // bus reset: duration of low phase [us]
      const int DurationReset = 480;
      rmt_item32_t tx_items[1] = {};
      present = false;

      tx_items[0].duration0 = DurationReset;
      tx_items[0].level0 = 0;
      tx_items[0].duration1 = 0;
      tx_items[0].level1 = 1;

      uint16_t old_rx_thresh = 0;
      ESP_ERROR_CHECK_WITHOUT_ABORT(_rxCh->getIdleThreshold(old_rx_thresh));
      ESP_ERROR_CHECK_WITHOUT_ABORT(
          _rxCh->setIdleThreshold(DurationReset + 60));

      flushRx();
      ESP_ERROR_CHECK_WITHOUT_ABORT(_rxCh->start(true));
      esp_err_t res =
          ESP_ERROR_CHECK_WITHOUT_ABORT(_txCh->write(tx_items, 1, true));
      if (res == ESP_OK) {
        size_t rx_size = 0;
        rmt_item32_t *rx_items = (rmt_item32_t *)xRingbufferReceive(
            _rb, &rx_size, 100 / portTICK_PERIOD_MS);

        if (rx_items) {
          if (rx_size >= (1 * sizeof(rmt_item32_t))) {
            // parse signal and search for presence pulse
            if ((rx_items[0].level0 == 0) &&
                (rx_items[0].duration0 >= DurationReset - 2))
              if ((rx_items[0].level1 == 1) && (rx_items[0].duration1 > 0))
                if (rx_items[1].level0 == 0)
                  present = true;
          }

          vRingbufferReturnItem(_rb, (void *)rx_items);
        } else
          res = ESP_ERROR_CHECK_WITHOUT_ABORT(ESP_ERR_TIMEOUT);
      }

      ESP_ERROR_CHECK_WITHOUT_ABORT(_rxCh->stop());
      ESP_ERROR_CHECK_WITHOUT_ABORT(_rxCh->setIdleThreshold(old_rx_thresh));
      return res;
    }

    void Driver::flushRx() {
      if (!_rb)
        return;
      void *p = nullptr;
      size_t s = 0;
      while ((p = xRingbufferReceive(_rb, &s, 0)))
        vRingbufferReturnItem(_rb, p);
    }

    void encode_read_slot(rmt_item32_t &item) {
      // construct pattern for a single read time slot
      item.level0 = 0;
      item.duration0 = Duration1Low;  // shortly force 0
      item.level1 = 1;
      item.duration1 = Duration1High;  // release high and finish slot
    }

    esp_err_t Driver::readBits(uint8_t *in, int number_of_bits_to_read) {
      if (!_rb)
        return ESP_FAIL;
      // sample time for read slot
      const int DurationSample = 15 - 2;

      rmt_item32_t tx_items[MaxBitsPerSlot + 1] = {};
      uint8_t read_data = 0;

      if (number_of_bits_to_read > MaxBitsPerSlot)
        return ESP_ERR_INVALID_ARG;

      // generate requested read slots
      for (int i = 0; i < number_of_bits_to_read; i++)
        encode_read_slot(tx_items[i]);

      // end marker
      tx_items[number_of_bits_to_read].level0 = 1;
      tx_items[number_of_bits_to_read].duration0 = 0;

      flushRx();
      ESP_ERROR_CHECK_WITHOUT_ABORT(_rxCh->start(true));
      esp_err_t res = ESP_ERROR_CHECK_WITHOUT_ABORT(
          _txCh->write(tx_items, number_of_bits_to_read + 1, true));
      if (res == ESP_OK) {
        size_t rx_size = 0;
        rmt_item32_t *rx_items =
            (rmt_item32_t *)xRingbufferReceive(_rb, &rx_size, portMAX_DELAY);

        if (rx_items) {
          if (rx_size >= number_of_bits_to_read * sizeof(rmt_item32_t)) {
            for (int i = 0; i < number_of_bits_to_read; i++) {
              read_data >>= 1;
              // parse signal and identify logical bit
              if (rx_items[i].level1 == 1)
                if ((rx_items[i].level0 == 0) &&
                    (rx_items[i].duration0 < DurationSample))
                  // rising edge occured before 15us -> bit 1
                  read_data |= 0x80;
            }
            read_data >>= 8 - number_of_bits_to_read;
          }

          vRingbufferReturnItem(_rb, (void *)rx_items);
        } else
          res = ESP_ERROR_CHECK_WITHOUT_ABORT(ESP_ERR_TIMEOUT);
      }

      ESP_ERROR_CHECK_WITHOUT_ABORT(_rxCh->stop());

      *in = read_data;
      return res;
    }

    void encode_write_slot(rmt_item32_t &item, uint8_t val) {
      item.level0 = 0;
      item.level1 = 1;
      if (val) {
        // write "1" slot
        item.duration0 = Duration1Low;
        item.duration1 = Duration1High;
      } else {
        // write "0" slot
        item.duration0 = Duration0Low;
        item.duration1 = Duration0High;
      }
    }

    esp_err_t Driver::writeBits(uint8_t out, int number_of_bits_to_write) {
      rmt_item32_t tx_items[MaxBitsPerSlot + 1] = {};

      if (number_of_bits_to_write > MaxBitsPerSlot)
        return ESP_ERR_INVALID_ARG;

      // write requested bits as pattern to TX buffer
      for (int i = 0; i < number_of_bits_to_write; i++) {
        encode_write_slot(tx_items[i], out & 0x01);
        out >>= 1;
      }

      // end marker
      tx_items[number_of_bits_to_write].level0 = 1;
      tx_items[number_of_bits_to_write].duration0 = 0;

      return ESP_ERROR_CHECK_WITHOUT_ABORT(
          _txCh->write(tx_items, number_of_bits_to_write + 1, true));
    }

    IDriver *newDeprecatedRmtDriver(gpio_num_t pin) {
      auto rx = deprecated::useRmtRx(pin);
      auto tx = deprecated::useRmtTx(pin);
      if (rx && tx)
        return new Driver(pin, rx, tx);
      if (rx)
        delete rx;
      if (tx)
        delete tx;
      return nullptr;
    }

  }  // namespace owb
}  // namespace esp32m

#endif