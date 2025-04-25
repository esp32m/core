#include <argtable3/argtable3.h>

#include "esp32m/json.hpp"
#include "esp32m/ui.hpp"
#include "esp32m/ui/console.hpp"

namespace esp32m {
  namespace ui {

    Console::Console() {
      _repl = nullptr;
      _repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
      _repl_config.prompt = "esp32m>";
    }

    Console::~Console() {
      if (_repl)
        _repl->del(_repl);
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_deinit());
    }

    Console &Console::instance() {
      static Console i;
      return i;
    }

    static struct {
      struct arg_str *name;
      struct arg_str *data;
      struct arg_str *target;
      struct arg_int *seq;
      struct arg_end *end;
    } request_args;

    int requestHandler(int argc, char **argv) {
      int nerrors = arg_parse(argc, argv, (void **)&request_args);
      if (nerrors != 0) {
        arg_print_errors(stderr, request_args.end, argv[0]);
        return 1;
      }
      JsonDocument *data = nullptr;
      if (request_args.data->count > 0)
        data = json::parse(request_args.data->sval[0]);
      /*size_t mu =
          JSON_OBJECT_SIZE(1)  // type:request
          + (data ? data->memoryUsage() : 0) +
          (request_args.name->count > 0
               ? (JSON_OBJECT_SIZE(1) + strlen(request_args.name->sval[0]) + 1)
               : 0) +
          (request_args.target->count > 0
               ? (JSON_OBJECT_SIZE(1) + strlen(request_args.target->sval[0]) +
                  1)
               : 0) +
          (request_args.seq->count > 0 ? JSON_OBJECT_SIZE(1) : 0);*/
      JsonDocument *doc = new JsonDocument(); /* mu */
      JsonObject root = doc->to<JsonObject>();
      root["type"] = "request";
      if (request_args.name->count > 0)
        root["name"] = (char *)request_args.name->sval[0];
      if (request_args.target->count > 0)
        root["target"] = (char *)request_args.target->sval[0];
      if (request_args.seq->count > 0)
        root["seq"] = request_args.seq->ival[0];
      if (data) {
        root["data"] = data->as<JsonVariant>();
        delete data;
      }
      Console::instance().incoming(0, doc);
      return 0;
    }

    void Console::init(Ui *ui) {
      Transport::init(ui);
#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || \
    defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
      esp_console_dev_uart_config_t _uart_config =
          ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
      ESP_ERROR_CHECK(
          esp_console_new_repl_uart(&_uart_config, &_repl_config, &_repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
      esp_console_dev_usb_cdc_config_t _cdc_config =
          ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
      ESP_ERROR_CHECK(
          esp_console_new_repl_usb_cdc(&_cdc_config, &_repl_config, &_repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
      esp_console_dev_usb_serial_jtag_config_t _usbjtag_config =
          ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
      ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(
          &_usbjtag_config, &_repl_config, &_repl));
#endif
      request_args.name = arg_str1(nullptr, nullptr, "<name>", "request name");
      request_args.data = arg_str0(nullptr, nullptr, "<data>", "JSON data");
      request_args.target = arg_str0("t", "target", "<target>", "target name");
      request_args.seq = arg_int0("s", "seq", "<sequence>", "sequence number");
      request_args.end = arg_end(3);
      esp_console_cmd_t request_cmd = {};
      request_cmd.command = "request";
      request_cmd.help = "send request to the ESP32 manager";
      request_cmd.func = &requestHandler;
      request_cmd.argtable = &request_args;
      ESP_ERROR_CHECK(esp_console_cmd_register(&request_cmd));
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_start_repl(_repl));
    }
    esp_err_t Console::wsSend(uint32_t cid, const char *text) {
      printf("%s\r\n", text);
      return ESP_OK;
    }
  }  // namespace ui
}  // namespace esp32m