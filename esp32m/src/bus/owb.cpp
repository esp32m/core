#include "esp32m/bus/owb.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/io/rmt.hpp"

#include <driver/gpio.h>
#include <driver/rmt.h>
#include <soc/gpio_periph.h>
#include <soc/gpio_struct.h>
#include <string.h>

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

    uint8_t crc(uint8_t c, uint8_t data) {
      // https://www.maximintegrated.com/en/app-notes/index.mvp/id/27
      static const uint8_t table[256] = {
          0,   94,  188, 226, 97,  63,  221, 131, 194, 156, 126, 32,  163, 253,
          31,  65,  157, 195, 33,  127, 252, 162, 64,  30,  95,  1,   227, 189,
          62,  96,  130, 220, 35,  125, 159, 193, 66,  28,  254, 160, 225, 191,
          93,  3,   128, 222, 60,  98,  190, 224, 2,   92,  223, 129, 99,  61,
          124, 34,  192, 158, 29,  67,  161, 255, 70,  24,  250, 164, 39,  121,
          155, 197, 132, 218, 56,  102, 229, 187, 89,  7,   219, 133, 103, 57,
          186, 228, 6,   88,  25,  71,  165, 251, 120, 38,  196, 154, 101, 59,
          217, 135, 4,   90,  184, 230, 167, 249, 27,  69,  198, 152, 122, 36,
          248, 166, 68,  26,  153, 199, 37,  123, 58,  100, 134, 216, 91,  5,
          231, 185, 140, 210, 48,  110, 237, 179, 81,  15,  78,  16,  242, 172,
          47,  113, 147, 205, 17,  79,  173, 243, 112, 46,  204, 146, 211, 141,
          111, 49,  178, 236, 14,  80,  175, 241, 19,  77,  206, 144, 114, 44,
          109, 51,  209, 143, 12,  82,  176, 238, 50,  108, 142, 208, 83,  13,
          239, 177, 240, 174, 76,  18,  145, 207, 45,  115, 202, 148, 118, 40,
          171, 245, 23,  73,  8,   86,  180, 234, 105, 55,  213, 139, 87,  9,
          235, 181, 54,  104, 138, 212, 149, 203, 41,  119, 244, 170, 72,  22,
          233, 183, 85,  11,  136, 214, 52,  106, 43,  117, 151, 201, 74,  20,
          246, 168, 116, 42,  200, 150, 21,  75,  169, 247, 182, 232, 10,  84,
          215, 137, 107, 53};

      return table[c ^ data];
    }

    uint8_t crc(uint8_t c, const uint8_t *buffer, size_t len) {
      do {
        c = crc(c, *buffer++);
      } while (--len > 0);
      return c;
    }

    class Driver {
     public:
      Driver(gpio_num_t pin, int index, io::RmtRx *rx, io::RmtTx *tx)
          : _gpio(pin), _index(index), _rxCh(rx), _txCh(tx) {
        /*int co = RMT_CHANNEL_0 + index * 2;
        _txCh = (rmt_channel_t)(co + 1);
        _rxCh = (rmt_channel_t)co;*/
        ESP_ERROR_CHECK_WITHOUT_ABORT(init());
      }
      ~Driver() {
        delete _rxCh;
        delete _txCh;
      }
      esp_err_t reset(bool &present);
      esp_err_t readBits(uint8_t *in, int number_of_bits_to_read);
      esp_err_t writeBits(uint8_t out, int number_of_bits_to_write);
      esp_err_t search(Search *state, bool *is_found);

     private:
      gpio_num_t _gpio;
      int _index;
      io::RmtRx *_rxCh;
      io::RmtTx *_txCh;

      int _refs = 0;
      bool _useCrc = true;
      bool _useParasiticPower;
      gpio_num_t _pullupGpio = GPIO_NUM_NC;
      RingbufHandle_t _rb = nullptr;

      esp_err_t init();
      void release();
      void flushRx();
      friend class esp32m::Owb;
      friend Driver *useDriver(gpio_num_t pin);
    };

    const int MaxDrivers = RMT_CHANNEL_MAX >> 1;
    Driver *_drivers[MaxDrivers] = {0};
    std::mutex _driversMutex;

    Driver *useDriver(gpio_num_t pin) {
      std::lock_guard guard(_driversMutex);
      Driver *found = nullptr;
      int i;
      for (i = 0; i < MaxDrivers; i++)
        if (_drivers[i] && _drivers[i]->_gpio == pin) {
          found = _drivers[i];
          break;
        }
      if (!found)
        for (i = 0; i < MaxDrivers; i++)
          if (!_drivers[i]) {
            auto rx = io::useRmtRx(pin);
            auto tx = io::useRmtTx(pin);
            if (rx && tx) {
              _drivers[i] = new Driver(pin, i, rx, tx);
              found = _drivers[i];
            } else {
              if (rx)
                delete rx;
              if (tx)
                delete tx;
            }
            break;
          }
      if (found)
        found->_refs++;
      return found;
    }

    void Driver::release() {
      std::lock_guard guard(_driversMutex);
      _refs--;
      if (_refs == 0) {
        _drivers[_index] = nullptr;
        delete this;
      }
    }

    char *toString(ROMCode &rom_code, char *buffer, size_t len) {
      for (int i = 0; i < sizeof(rom_code.bytes) && len >= 2; i++) {
        sprintf(buffer, "%02x", rom_code.bytes[i]);
        buffer += 2;
        len -= 2;
      }
      return buffer;
    }

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

      // attach GPIO to previous pin
      if (_gpio < 32)
        GPIO.enable_w1ts = (0x1 << _gpio);
      else
        GPIO.enable1_w1ts.data = (0x1 << (_gpio - 32));

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
      PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[_gpio]);

      // enable open drain
      GPIO.pin[_gpio].pad_driver = 1;
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

    esp_err_t Driver::search(Search *state, bool *is_found) {
      // initialize for search
      int id_bit_number = 1;
      int last_zero = 0;
      int rom_byte_number = 0;
      uint8_t id_bit = 0;
      uint8_t cmp_id_bit = 0;
      uint8_t rom_byte_mask = 1;
      uint8_t search_direction = 0;
      bool search_result = false;
      uint8_t crc8 = 0;

      // if the last call was not the last one
      if (!state->_lastDeviceFlag) {
        // 1-Wire reset
        bool is_present;
        ESP_CHECK_RETURN(reset(is_present));
        if (!is_present) {
          // reset the search
          state->_lastDiscrepancy = 0;
          state->_lastDeviceFlag = false;
          state->_lastFamilyDiscrepancy = 0;
          *is_found = false;
          return ESP_OK;
        }

        // issue the search command
        writeBits(RomSearch, 8);

        // loop to do the search
        do {
          id_bit = cmp_id_bit = 0;

          // read a bit and its complement
          readBits(&id_bit, 1);
          readBits(&cmp_id_bit, 1);

          // check for no devices on 1-wire (signal level is high in both bit
          // reads)
          if (id_bit && cmp_id_bit)
            break;
          else {
            // all devices coupled have 0 or 1
            if (id_bit != cmp_id_bit)
              search_direction =
                  (id_bit) ? 1 : 0;  // bit write value for search
            else {
              // if this discrepancy if before the Last Discrepancy
              // on a previous next then pick the same as last time
              if (id_bit_number < state->_lastDiscrepancy)
                search_direction = ((state->_romCode.bytes[rom_byte_number] &
                                     rom_byte_mask) > 0);
              else
                // if equal to last pick 1, if not then pick 0
                search_direction = (id_bit_number == state->_lastDiscrepancy);

              // if 0 was picked then record its position in LastZero
              if (search_direction == 0) {
                last_zero = id_bit_number;

                // check for Last discrepancy in family
                if (last_zero < 9)
                  state->_lastFamilyDiscrepancy = last_zero;
              }
            }

            // set or clear the bit in the ROM byte rom_byte_number
            // with mask rom_byte_mask
            if (search_direction == 1)
              state->_romCode.bytes[rom_byte_number] |= rom_byte_mask;
            else
              state->_romCode.bytes[rom_byte_number] &= ~rom_byte_mask;
            // serial number search direction write bit
            writeBits(search_direction, 1);

            // increment the byte counter id_bit_number
            // and shift the mask rom_byte_mask
            id_bit_number++;
            rom_byte_mask <<= 1;

            // if the mask is 0 then go to new SerialNum byte rom_byte_number
            // and reset mask
            if (rom_byte_mask == 0) {
              crc8 = crc(crc8,
                         state->_romCode
                             .bytes[rom_byte_number]);  // accumulate the CRC
              rom_byte_number++;
              rom_byte_mask = 1;
            }
          }
        } while (rom_byte_number < 8);  // loop until through all ROM bytes 0-7

        // if the search was successful then
        if (!((id_bit_number < 65) || (crc8 != 0))) {
          // search successful so set
          // LastDiscrepancy,LastDeviceFlag,search_result
          state->_lastDiscrepancy = last_zero;

          // check for last device
          if (state->_lastDiscrepancy == 0)
            state->_lastDeviceFlag = true;

          search_result = true;
        }
      }

      // if no device found then reset counters so next 'search' will be like a
      // first
      if (!search_result || !state->_romCode.bytes[0]) {
        state->_lastDiscrepancy = 0;
        state->_lastDeviceFlag = false;
        state->_lastFamilyDiscrepancy = 0;
        search_result = false;
      }

      *is_found = search_result;

      return ESP_OK;
    }

    Search::Search(Owb *owb) : _owb(owb) {}

    ROMCode *Search::next() {
      if ((_started && !_found) || _error)
        return nullptr;
      if (!_started) {
        memset(&_romCode, 0, sizeof(_romCode));
        _lastDiscrepancy = 0;
        _lastFamilyDiscrepancy = 0;
        _lastDeviceFlag = false;
        _started = true;
      }
      _error = _owb->_driver->search(this, &_found);
      return _found && !_error ? &_romCode : nullptr;
    }
  }  // namespace owb

  Owb::Owb(gpio_num_t pin) : _mutex(locks::get(pin)) {
    _driver = owb::useDriver(pin);
  }

  Owb::~Owb() {
    if (_driver)
      _driver->release();
  }

  esp_err_t Owb::reset(bool &present) {
    return _driver->reset(present);
  }

  esp_err_t Owb::read(bool &value) {
    return _driver->readBits((uint8_t *)&value, 1);
  }

  esp_err_t Owb::read(uint8_t &value) {
    return _driver->readBits((uint8_t *)&value, 8);
  }

  esp_err_t Owb::read(void *buffer, size_t size) {
    for (int i = 0; i < size; ++i) {
      uint8_t out;
      esp_err_t res = _driver->readBits(&out, 8);
      if (res != ESP_OK)
        return res;
      ((uint8_t *)buffer)[i] = out;
    }
    return ESP_OK;
  }

  esp_err_t Owb::write(bool value) {
    return _driver->writeBits(value ? 1 : 0, 1);
  }

  esp_err_t Owb::write(uint8_t value) {
    return _driver->writeBits(value, 8);
  }

  esp_err_t Owb::write(const void *buffer, size_t size) {
    for (int i = 0; i < size; i++) {
      esp_err_t res = _driver->writeBits(((uint8_t *)buffer)[i], 8);
      if (res != ESP_OK)
        return res;
    }
    return ESP_OK;
  }

  esp_err_t Owb::write(const owb::ROMCode &code) {
    return write(code.bytes, sizeof(code.bytes));
  }

}  // namespace esp32m