#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = 0x102,
    ESP_ERR_INVALID_STATE = 0x103, ESP_ERR_INVALID_SIZE = 0x104,
    ESP_ERR_TIMEOUT = 0x107, ESP_ERR_INVALID_RESPONSE = 0x108;
#define ESP_CHECK_RETURN(expr) do { const auto err_ = (expr); if (err_ != ESP_OK) return err_; } while (0)
template<class... T> inline void fakeLog(T...) {}
#define logI(...) fakeLog(__VA_ARGS__)
#define logW(...) fakeLog(__VA_ARGS__)
#define logE(...) fakeLog(__VA_ARGS__)
#define SOC_UART_HP_NUM 3
#define CONFIG_IDF_TARGET_ESP32S3 0
using uart_port_t = int;
using uart_parity_t = int;
using TickType_t = uint32_t;
#define pdMS_TO_TICKS(ms) ((ms) / 10)
constexpr int UART_NUM_0 = 0, UART_NUM_1 = 1, UART_NUM_2 = 2,
  UART_PARITY_DISABLE = 0, UART_PARITY_EVEN = 1, UART_DATA_8_BITS = 8,
  UART_STOP_BITS_1 = 1, UART_PIN_NO_CHANGE = -1,
  UART_MODE_RS485_HALF_DUPLEX = 1, UART_HW_FLOWCTRL_DISABLE = 0,
  UART_SCLK_DEFAULT = 0, MB_ASCII = 1, MB_RTU = 0,
  MB_PARAM_INPUT = 0, PARAM_TYPE_U32 = 0, PAR_PERMS_READ_WRITE_TRIGGER = 0;
struct serial_opts { int mode, port; uint32_t baudrate; int parity;
  uint32_t response_tout_ms; int data_bits, stop_bits; };
struct mb_communication_info_t { serial_opts ser_opts; };
struct mb_parameter_descriptor_t { int id; const char* key; const char* unit;
  int addr, type, start, count, offset, datatype, size, opts, perm; };
struct mb_param_request_t { uint8_t slave_addr, command; uint16_t reg_start, reg_size; };
struct uart_config_t { int baud_rate, data_bits, parity, stop_bits, flow_ctrl,
  rx_flow_ctrl_thresh, source_clk; };
esp_err_t mbc_master_create_serial(mb_communication_info_t*, void**);
esp_err_t mbc_master_set_descriptor(void*, const mb_parameter_descriptor_t*, int);
esp_err_t mbc_master_start(void*);
esp_err_t mbc_master_stop(void*);
esp_err_t mbc_master_delete(void*);
esp_err_t mbc_master_send_request(void*, mb_param_request_t*, void*);
esp_err_t uart_set_pin(uart_port_t, int, int, int, int);
esp_err_t uart_set_mode(uart_port_t, int);
esp_err_t uart_set_rx_timeout(uart_port_t, uint8_t);
esp_err_t uart_flush_input(uart_port_t);
esp_err_t uart_param_config(uart_port_t, const uart_config_t*);
esp_err_t uart_driver_install(uart_port_t, int, int, int, void*, int);
esp_err_t uart_driver_delete(uart_port_t);
bool uart_is_driver_installed(uart_port_t);
int uart_write_bytes(uart_port_t, const void*, size_t);
esp_err_t uart_wait_tx_done(uart_port_t, TickType_t);
int uart_read_bytes(uart_port_t, void*, size_t, TickType_t);
int64_t esp_timer_get_time();
void vTaskDelay(TickType_t);
namespace esp32m {
  void delay(uint32_t);
  namespace locks { std::mutex& uart(uart_port_t); }
  namespace log { class Loggable { public: virtual const char* name() const = 0; }; }
}
