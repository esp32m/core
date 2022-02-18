#include <esp_task_wdt.h>
#include <mbedtls/base64.h>

#include <esp32m/app.hpp>

#include "esp32m/dev/uart.hpp"

namespace esp32m {

  namespace dev {

    Uart::Uart(int num, uart_config_t &config) : _num(num) {
      sprintf(_name, "uart%d", num);
      ESP_ERROR_CHECK_WITHOUT_ABORT(uart_wait_tx_idle_polling(num));
      ESP_ERROR_CHECK_WITHOUT_ABORT(uart_param_config(num, &config));
      ESP_ERROR_CHECK_WITHOUT_ABORT(
          uart_set_pin(num, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    }

    Uart::~Uart() {
      _stopped = true;
      while (_task) vTaskDelay(1);
      uart_driver_delete(_num);
    }

    bool Uart::handleEvent(Event &ev) {
      if (EventInit::is(ev, 0)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            uart_driver_install(_num, 2048, 2048, 10, &_queue, 0));
        // ESP_ERROR_CHECK_WITHOUT_ABORT(uart_set_mode(num,
        // UART_MODE_RS485_HALF_DUPLEX));
        xTaskCreate([](void *self) { ((Uart *)self)->run(); }, "m/uart", 4096,
                    this, tskIDLE_PRIORITY, &_task);
        logI("server started");
        return true;
      }
      return false;
    }

    void Uart::run() {
      esp_task_wdt_add(NULL);
      auto buf = (uint8_t *)malloc(2048);
      while (!_stopped) {
        esp_task_wdt_reset();
        uart_event_t event;
        int ret = xQueueReceive(_queue, (void *)&event,
                                (TickType_t)10 / portTICK_PERIOD_MS);
        if (ret != pdPASS)
          continue;
        if (event.type == UART_DATA) {
          ret = uart_read_bytes(_num, buf, 2048,
                                (TickType_t)10 / portTICK_PERIOD_MS);
          if (ret > 0) {
            std::lock_guard<std::mutex> guard(_mutex);
            if (!_pendingResponse)
              continue;
            size_t olen = 0;
            uint8_t *b64buf = nullptr;
            size_t slen = ret;
            ret = mbedtls_base64_encode(b64buf, olen, &olen, buf, slen);
            if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && olen) {
              b64buf = (uint8_t *)malloc(olen + 1);
              ret = ESP_ERROR_CHECK_WITHOUT_ABORT(
                  mbedtls_base64_encode(b64buf, olen, &olen, buf, slen));
              if (ret == 0) {
                b64buf[olen] = 0;
                DynamicJsonDocument *doc = new DynamicJsonDocument(olen + 1);
                doc->set(b64buf);
                _pendingResponse->setData(doc);
                _pendingResponse->publish();
              }
              free(b64buf);
            }
          }
        }
      }
      free(buf);
      vTaskDelete(NULL);
      _task = nullptr;
    }

    bool Uart::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is(name(), "write")) {
        auto data = req.data().as<const char *>();
        if (data) {
          size_t slen = strlen(data);
          size_t olen = 0;
          uint8_t *binbuf = nullptr;
          int ret = mbedtls_base64_decode(binbuf, olen, &olen,
                                          (const uint8_t *)data, slen);
          if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && olen) {
            binbuf = (uint8_t *)malloc(olen);
            ret = ESP_ERROR_CHECK_WITHOUT_ABORT(mbedtls_base64_decode(
                binbuf, olen, &olen, (const uint8_t *)data, slen));
            if (ret == 0) {
              std::lock_guard<std::mutex> guard(_mutex);
              if (_pendingResponse)
                delete _pendingResponse;
              _pendingResponse = req.makeResponse();
              ret = uart_write_bytes(_num, binbuf, olen);
            }
            free(binbuf);
          }
        }
        return true;
      }
      return false;
    }

    void useUart(int num, uart_config_t &config) {
      new Uart(num, config);
    }

  }  // namespace dev
}  // namespace esp32m