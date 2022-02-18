#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

#include <driver/rmt.h>
#include <mutex>

namespace esp32m {
  namespace io {
    class Rmt {
     public:
      Rmt(const Rmt&) = delete;
      virtual ~Rmt();
      rmt_channel_t channel() const {
        return _cfg.channel;
      }
      gpio_num_t gpio() const {
        return _cfg.gpio_num;
      }
      rmt_mode_t mode() const {
        return _cfg.rmt_mode;
      }
      int getIntrFlags() const {
        return _intrFlags;
      }
      void setIntrFlags(int flags) {
        _intrFlags = flags;
      }
      esp_err_t setGpio(bool invert);
      esp_err_t configure();
      virtual esp_err_t install() = 0;
      esp_err_t uninstall();

     protected:
      Rmt() : _configDirty(true), _installed(false), _invertSignal(false) {}
      rmt_config_t _cfg;
      int _intrFlags =
          ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_SHARED;
      uint8_t _configDirty : 1;
      uint8_t _installed : 1;
      uint8_t _invertSignal : 1;
    };

    class RmtRx : public Rmt {
     public:
      size_t getBufSize() const {
        return _bufSize;
      }
      void setBufSize(size_t value) {
        _bufSize = value;
      }
      esp_err_t getRingbufHandle(RingbufHandle_t* buf_handle);
      esp_err_t enableFilter(uint8_t thresh);
      esp_err_t disableFilter();
      esp_err_t getIdleThreshold(uint16_t& thresh);
      esp_err_t setIdleThreshold(uint16_t thresh);
      esp_err_t install() override;
      esp_err_t start(bool reset);
      esp_err_t stop();

     private:
      RmtRx(rmt_channel_t ch, gpio_num_t gpio, size_t bufsize);
      size_t _bufSize;
      friend RmtRx* useRmtRx(gpio_num_t gpio, size_t bufsize);
    };

    class RmtTx : public Rmt {
     public:
      esp_err_t enableIdle(rmt_idle_level_t level);
      esp_err_t disableIdle();
      esp_err_t enableCarrier(uint16_t high, uint16_t low,
                              rmt_carrier_level_t level);
      esp_err_t disableCarrier();
      esp_err_t install() override;
      esp_err_t start(bool reset);
      esp_err_t stop();
      esp_err_t write(rmt_item32_t* rmt_item, int item_num, bool wait_tx_done);
      esp_err_t wait(TickType_t time);

     private:
      RmtTx(rmt_channel_t ch, gpio_num_t gpio);
      friend RmtTx* useRmtTx(gpio_num_t gpio);
    };

    RmtRx* useRmtRx(gpio_num_t gpio, size_t bufsize = 512);
    RmtTx* useRmtTx(gpio_num_t gpio);

  }  // namespace io
};   // namespace esp32m