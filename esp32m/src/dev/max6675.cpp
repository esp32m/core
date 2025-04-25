#include "esp32m/dev/max6675.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {

    Max6675::Max6675(const char *name, spi_host_device_t host)
        : _name(name ? name : "MAX6675"),
          _host(host) { Device::init(Flags::HasSensors); }

    bool Max6675::initSensors() {
      spi_bus_config_t buscfg = {};
      spi_device_interface_config_t devCfg = {};
      switch (_host) {
#if SOC_SPI_PERIPH_NUM > 2
        case SPI3_HOST:
          buscfg.miso_io_num = 19;
          // buscfg.mosi_io_num = 23;
          buscfg.sclk_io_num = 18;
          devCfg.spics_io_num = 5;
          break;
#endif
        case SPI2_HOST:
          buscfg.miso_io_num = 12;
          // buscfg.mosi_io_num = 13;
          buscfg.sclk_io_num = 14;
          devCfg.spics_io_num = 15;
          break;
        default:
          return false;
      }
      buscfg.mosi_io_num = -1;  // MAX6775 doesn't have slave input
      buscfg.quadwp_io_num = -1;
      buscfg.quadhd_io_num = -1;
      buscfg.max_transfer_sz = (4 * 8);

      // Initialize the SPI bus
      esp_err_t ret = spi_bus_initialize(_host, &buscfg, SPI_DMA_CH_AUTO);
      ESP_CHECK_RETURN_BOOL(ret);

      devCfg.mode = 0;
      devCfg.clock_speed_hz = 2 * 1000 * 1000;
      devCfg.queue_size = 3;

      ret = spi_bus_add_device(_host, &devCfg, &_spi);
      ESP_CHECK_RETURN_BOOL(ret);
      return true;
    }

    JsonDocument *Max6675::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(2) */
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_value);
      return doc;
    }

    bool Max6675::pollSensors() {
      if (!_spi)
        return false;
      uint16_t data = 0;
      spi_transaction_t tM = {};
      tM.tx_buffer = NULL;
      tM.rx_buffer = &data;
      tM.length = 16;
      tM.rxlength = 16;
      auto err = ESP_ERROR_CHECK_WITHOUT_ABORT(
          spi_device_acquire_bus(_spi, portMAX_DELAY));
      if (err == ESP_OK) {
        err = ESP_ERROR_CHECK_WITHOUT_ABORT(spi_device_transmit(_spi, &tM));
        spi_device_release_bus(_spi);
      }

      if (err == ESP_OK) {
        uint16_t res = SPI_SWAP_DATA_RX(data, 16);
        if ((res & (1 << 14)) == 0) {
          res >>= 3;
          _value = res * 0.25;
          _stamp = millis();
        }  // else logW("sensor is not connected");
        sensor("temperature", _value);
      }
      return true;
    }

    Max6675 *useMax6675(const char *name, spi_host_device_t host) {
      return new Max6675(name, host);
    }
  }  // namespace dev
}  // namespace esp32m