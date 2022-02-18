#include "esp32m/io/rmt.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {
  namespace io {

    std::mutex rmtChannelsMutex;
    uint8_t rmtChannels = 0;

    esp_err_t Rmt::uninstall() {
      if (!_installed)
        return ESP_OK;
      ESP_CHECK_RETURN(rmt_driver_uninstall(_cfg.channel));
      _installed = false;
      return ESP_OK;
    }

    Rmt::~Rmt() {
      ESP_ERROR_CHECK_WITHOUT_ABORT(uninstall());
      std::lock_guard lock(rmtChannelsMutex);
      rmtChannels &= ~(1 << _cfg.channel);
    }

    esp_err_t Rmt::setGpio(bool invert) {
      _invertSignal = invert;
      return rmt_set_gpio(_cfg.channel, _cfg.rmt_mode, _cfg.gpio_num,
                          _invertSignal);
    }

    esp_err_t Rmt::configure() {
      ESP_CHECK_RETURN(rmt_config(&_cfg));
      _configDirty = false;
      return ESP_OK;
    }

    RmtRx::RmtRx(rmt_channel_t ch, gpio_num_t gpio, size_t bufsize) {
      _cfg = (rmt_config_t)RMT_DEFAULT_CONFIG_RX(gpio, ch);
      _bufSize = bufsize;
      _configDirty = true;
    }

    esp_err_t RmtRx::getRingbufHandle(RingbufHandle_t* buf_handle) {
      return rmt_get_ringbuf_handle(_cfg.channel, buf_handle);
    }
    esp_err_t RmtRx::enableFilter(uint8_t thresh) {
      _cfg.rx_config.filter_en = true;
      _cfg.rx_config.filter_ticks_thresh = thresh;
      if (_configDirty)
        return ESP_OK;
      return rmt_set_rx_filter(_cfg.channel, true, thresh);
    }

    esp_err_t RmtRx::disableFilter() {
      _cfg.rx_config.filter_en = false;
      if (_configDirty)
        return ESP_OK;
      return rmt_set_rx_filter(_cfg.channel, false,
                               _cfg.rx_config.filter_ticks_thresh);
    }

    esp_err_t RmtRx::getIdleThreshold(uint16_t& thresh) {
      ESP_CHECK_RETURN(
          rmt_get_rx_idle_thresh(_cfg.channel, &_cfg.rx_config.idle_threshold));
      thresh = _cfg.rx_config.idle_threshold;
      return ESP_OK;
    }

    esp_err_t RmtRx::setIdleThreshold(uint16_t thresh) {
      _cfg.rx_config.idle_threshold = thresh;
      if (_configDirty)
        return ESP_OK;
      return rmt_set_rx_idle_thresh(_cfg.channel, thresh);
    }

    esp_err_t RmtRx::install() {
      if (_configDirty)
        ESP_CHECK_RETURN(configure());
      if (_installed)
        return ESP_OK;
      ESP_CHECK_RETURN(rmt_driver_install(_cfg.channel, _bufSize, _intrFlags));
      _installed = true;
      return ESP_OK;
    }
    esp_err_t RmtRx::start(bool reset) {
      return rmt_rx_start(_cfg.channel, reset);
    }
    esp_err_t RmtRx::stop() {
      return rmt_rx_stop(_cfg.channel);
    }

    RmtTx::RmtTx(rmt_channel_t ch, gpio_num_t gpio) {
      _cfg = (rmt_config_t)RMT_DEFAULT_CONFIG_TX(gpio, ch);
      _configDirty = true;
    }

    esp_err_t RmtTx::enableIdle(rmt_idle_level_t level) {
      _cfg.tx_config.idle_output_en = true;
      _cfg.tx_config.idle_level = level;
      if (_configDirty)
        return ESP_OK;
      return rmt_set_idle_level(_cfg.channel, true, level);
    }
    esp_err_t RmtTx::disableIdle() {
      _cfg.tx_config.idle_output_en = false;
      if (_configDirty)
        return ESP_OK;
      return rmt_set_idle_level(_cfg.channel, false, _cfg.tx_config.idle_level);
    }
    esp_err_t RmtTx::enableCarrier(uint16_t high, uint16_t low,
                                   rmt_carrier_level_t level) {
      _cfg.tx_config.carrier_en = true;
      _cfg.tx_config.carrier_level = level;
      return rmt_set_tx_carrier(_cfg.channel, true, high, low, level);
    }
    esp_err_t RmtTx::disableCarrier() {
      _cfg.tx_config.carrier_en = false;
      if (_configDirty)
        return ESP_OK;
      return rmt_set_tx_carrier(_cfg.channel, true, 0, 0,
                                RMT_CARRIER_LEVEL_HIGH);
    }

    esp_err_t RmtTx::install() {
      if (_configDirty)
        ESP_CHECK_RETURN(configure());
      if (_installed)
        return ESP_OK;
      ESP_CHECK_RETURN(rmt_driver_install(_cfg.channel, 0, _intrFlags));
      _installed = true;
      return ESP_OK;
    }

    esp_err_t RmtTx::start(bool reset) {
      return rmt_tx_start(_cfg.channel, reset);
    }
    esp_err_t RmtTx::stop() {
      return rmt_tx_stop(_cfg.channel);
    }
    esp_err_t RmtTx::write(rmt_item32_t* rmt_item, int item_num,
                           bool wait_tx_done) {
      return rmt_write_items(_cfg.channel, rmt_item, item_num, wait_tx_done);
    }
    esp_err_t RmtTx::wait(TickType_t time) {
      return rmt_wait_tx_done(_cfg.channel, time);
    }
    int useRmtChannel() {
      std::lock_guard lock(rmtChannelsMutex);
      int ch = -1;
      for (auto i = 0; i < RMT_CHANNEL_MAX; i++)
        if ((rmtChannels & (1 << i)) == 0) {
          ch = i;
          break;
        }
      if (ch < 0)
        return -1;
      rmtChannels |= (1 << ch);
      return ch;
    }

    RmtRx* useRmtRx(gpio_num_t gpio, size_t bufsize) {
      int ch = useRmtChannel();
      if (ch < 0)
        return nullptr;
      return new RmtRx((rmt_channel_t)ch, gpio, bufsize);
    }
    RmtTx* useRmtTx(gpio_num_t gpio) {
      int ch = useRmtChannel();
      if (ch < 0)
        return nullptr;
      return new RmtTx((rmt_channel_t)ch, gpio);
    }

  }  // namespace io
}  // namespace esp32m
