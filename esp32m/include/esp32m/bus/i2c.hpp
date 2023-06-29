#pragma once

#include <map>
#include <memory>
#include <mutex>

#include <driver/i2c.h>
#include <hal/gpio_types.h>
#include <sdkconfig.h>

#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {

  namespace i2c {

    struct Mutex {
      std::mutex _mutex;
      uint8_t _refs;
    };

    class MutexRef {
     public:
      MutexRef(uint8_t addr, i2c_port_t port = I2C_NUM_0,
               int sda = I2C_MASTER_SDA, int scl = I2C_MASTER_SCL);
      ~MutexRef();
      std::mutex &mutex();

     private:
      uint32_t _key;
    };

  }  // namespace i2c

  class I2C : public log::Loggable {
   public:
    I2C(uint8_t addr, i2c_port_t port = I2C_NUM_0,
        gpio_num_t sda = I2C_MASTER_SDA, gpio_num_t scl = I2C_MASTER_SCL,
        uint32_t clk_speed = 100000);
    I2C(const I2C &) = delete;
    const char *name() const override {
      return _name.c_str();
    }
    std::mutex &mutex() const {
      return _mutex->mutex();
    }
    uint8_t addr() const {
      return _addr;
    }
    void setAddr(uint8_t addr);
    i2c_port_t port() const {
      return _port;
    }
    uint32_t clkSpeed() const {
      return _cfg.master.clk_speed;
    }
    void setClkSpeed(uint32_t speed) {
      _cfg.master.clk_speed = speed;
    }
    Endian endianness() const {
      return _endianness;
    }
    void setEndianness(Endian endianness) {
      _endianness = endianness;
    }
    esp_err_t read(const void *out_data, size_t out_size, void *in_data,
                   size_t in_size);
    esp_err_t write(const void *out_reg, size_t out_reg_size,
                    const void *out_data, size_t out_size);
    void setErrSnooze(int ms) {
      _errSnooze = ms;
    }
    bool ready() {
      return _err == ESP_OK || !_errSnooze ||
             (millis() - _errStamp > _errSnooze);
    }
    inline esp_err_t read(uint8_t reg, void *in_data, size_t in_size) {
      return read(&reg, 1, in_data, in_size);
    }
    inline esp_err_t write(uint8_t reg, const void *out_data, size_t out_size) {
      return write(&reg, 1, out_data, out_size);
    }
    inline esp_err_t read(uint8_t reg, uint8_t &value) {
      return read(&reg, 1, &value, 1);
    }
    inline esp_err_t write(uint8_t reg, uint8_t value) {
      return write(&reg, 1, &value, 1);
    }
    template <class T>
    esp_err_t read(uint8_t reg, T &value) {
      T v;
      ESP_CHECK_RETURN(read(&reg, 1, &v, sizeof(T)));
      value = fromEndian(_endianness, v);
      return ESP_OK;
    }
    template <class T>
    esp_err_t write(uint8_t reg, const T value) {
      T v = toEndian(_endianness, value);
      return write(&reg, 1, &v, sizeof(T));
    }

    esp_err_t readSafe(uint8_t reg, uint8_t &value) {
      std::lock_guard guard(mutex());
      return read(&reg, 1, &value, 1);
    }
    esp_err_t writeSafe(uint8_t reg, uint8_t value) {
      std::lock_guard guard(mutex());
      return write(&reg, 1, &value, 1);
    }
    template <class T>
    esp_err_t readSafe(uint8_t reg, T &value) {
      std::lock_guard guard(mutex());
      T v;
      ESP_CHECK_RETURN(read(&reg, 1, &v, sizeof(T)));
      value = fromEndian(_endianness, v);
      return ESP_OK;
    }
    template <class T>
    esp_err_t writeSafe(uint8_t reg, const T value) {
      std::lock_guard guard(mutex());
      T v = toEndian(_endianness, value);
      return write(&reg, 1, &v, sizeof(T));
    }
    esp_err_t sync();

   private:
    std::string _name;
    uint8_t _addr = 0;
    i2c_port_t _port;
    i2c_config_t _cfg;
    esp_err_t _err = ESP_OK;
    unsigned long _errStamp = 0;
    int _errSnooze = 0;

    Endian _endianness = Endian::Big;
    std::unique_ptr<i2c::MutexRef> _mutex;
    void logIO(bool write, esp_err_t res, const void *out_data,
               size_t out_size, const void *in_data, size_t in_size);
  };

}  // namespace esp32m