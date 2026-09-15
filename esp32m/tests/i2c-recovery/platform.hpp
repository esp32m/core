#pragma once

#include <cassert>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <string>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = 0x102,
                    ESP_ERR_INVALID_STATE = 0x103, ESP_ERR_NOT_FOUND = 0x105,
                    ESP_ERR_TIMEOUT = 0x107, ESP_ERR_INVALID_RESPONSE = 0x108;
inline esp_err_t fakeCheck(esp_err_t error) { return error; }
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expr) fakeCheck(expr)
#define ESP_CHECK_RETURN(expr) do { const auto error = (expr); if (error != ESP_OK) return error; } while (0)

constexpr int I2C_NUM_0 = 0, I2C_MASTER_SDA = 21, I2C_MASTER_SCL = 22,
              I2C_CLK_SRC_DEFAULT = 0, I2C_ADDR_BIT_LEN_7 = 0;
struct FakeBus {};
struct FakeDevice { FakeBus* bus; };
using i2c_master_bus_handle_t = FakeBus*;
using i2c_master_dev_handle_t = FakeDevice*;
struct i2c_master_bus_config_t {
  int i2c_port, sda_io_num, scl_io_num, clk_source, glitch_ignore_cnt,
      intr_priority, trans_queue_depth;
  struct { bool enable_internal_pullup, allow_pd; } flags;
};
struct i2c_device_config_t {
  int dev_addr_length;
  uint16_t device_address;
  uint32_t scl_speed_hz, scl_wait_us;
  struct { bool disable_ack_check; } flags;
};
struct i2c_master_transmit_multi_buffer_info_t {
  uint8_t* write_buffer;
  size_t buffer_size;
};
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*, i2c_master_bus_handle_t*);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t, uint16_t, int);
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t);
esp_err_t i2c_master_bus_wait_all_done(i2c_master_bus_handle_t, int);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t*, i2c_master_dev_handle_t*);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t*, size_t, uint8_t*, size_t, int);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t, uint8_t*, size_t, int);
esp_err_t i2c_master_multi_buffer_transmit(i2c_master_dev_handle_t, i2c_master_transmit_multi_buffer_info_t*, size_t, int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t*, size_t, int);

using TickType_t = uint32_t;
// Match greenhouse's 100 Hz RTOS tick, where the old 2 ms delay became zero.
#define pdMS_TO_TICKS(ms) ((ms) / 10)
void vTaskDelay(TickType_t ticks);

namespace esp32m {
  unsigned long millis();
  void delay(uint32_t);
  std::string string_printf(const char*, ...);
  enum Endian { Little, Big };
  template <class T> T fromEndian(Endian endian, T value) {
    if (endian == Little || sizeof(T) == 1) return value;
    T result = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
      result = static_cast<T>((result << 8) | (value & 0xff));
      value >>= 8;
    }
    return result;
  }
  template <class T> T toEndian(Endian endian, T value) { return fromEndian(endian, value); }
  namespace log {
    class Loggable {
     public:
      virtual ~Loggable() = default;
      virtual const char* name() const = 0;
    };
  }
}
