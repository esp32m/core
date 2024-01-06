#include <esp_console.h>

#include "esp32m/ui/transport.hpp"

namespace esp32m {
  namespace ui {

    class Console : public Transport {
     public:
      virtual ~Console();
      static Console& instance();
      const char* name() const override {
        return "ui-console";
      };

     protected:
      void init(Ui* ui) override;
      esp_err_t wsSend(uint32_t cid, const char* text) override;

     private:
      Console();
      esp_console_repl_t* _repl;
      esp_console_repl_config_t _repl_config;
      #if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
          esp_console_dev_uart_config_t _uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
      #elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
          esp_console_dev_usb_cdc_config_t _uart_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
      #elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
          esp_console_dev_usb_serial_jtag_config_t _uart_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
      #endif
      friend int requestHandler(int argc, char** argv);
    };

  }  // namespace ui
}  // namespace esp32m