#pragma once

#include <map>
#include <mutex>

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {

  namespace i2c {

    class MasterDev;

    class MasterBus {
     public:
      ~MasterBus() {
        // Registry operations always precede the per-bus lock.
        std::lock_guard busesLock(_busesMutex);
        std::lock_guard busLock(_mutex);
        assert(_devices.size() == 0);
        if (_handle) {
          _buses.erase(_handle);
          ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_del_master_bus(_handle));
          _handle = nullptr;
        }
      }

      esp_err_t probe(uint16_t address, int xfer_timeout_ms = 100) const {
        std::lock_guard lock(_mutex);
        return i2c_master_probe(_handle, address, xfer_timeout_ms);
      }

      esp_err_t reset() {
        std::lock_guard lock(_mutex);
        return i2c_master_bus_reset(_handle);
      }

      // Synchronous operations, including recovery, share this lock. Owners
      // must keep the bus/devices alive while other tasks use them.
      std::recursive_mutex& mutex() const {
        return _mutex;
      }

      esp_err_t find(uint16_t address, MasterDev** dev = nullptr);

      static MasterBus* getDefault() {
        std::lock_guard lock(_busesMutex);
        if (_buses.size() == 0) {
          i2c_master_bus_config_t config = {
              .i2c_port = I2C_NUM_0,
              .sda_io_num = I2C_MASTER_SDA,
              .scl_io_num = I2C_MASTER_SCL,
              .clk_source = I2C_CLK_SRC_DEFAULT,
              .glitch_ignore_cnt = 7,
              .intr_priority = 0,
              .trans_queue_depth = 0,
              .flags = {.enable_internal_pullup = true, .allow_pd = false}};
          i2c_master_bus_handle_t handle;
          if (ESP_ERROR_CHECK_WITHOUT_ABORT(
                  i2c_new_master_bus(&config, &handle)) != ESP_OK)
            return nullptr;
          return new MasterBus(handle, config);
        } else
          return _buses.begin()->second;
      }

      static esp_err_t New(i2c_master_bus_config_t& config,
                           MasterBus** bus = nullptr) {
        std::lock_guard lock(_busesMutex);
        i2c_master_bus_handle_t handle;
        ESP_CHECK_RETURN(i2c_new_master_bus(&config, &handle));
        auto masterBus = new MasterBus(handle, config);
        if (bus)
          *bus = masterBus;
        return ESP_OK;
      }

     private:
      MasterBus(i2c_master_bus_handle_t handle, i2c_master_bus_config_t& config)
          : _handle(handle), _config(config) {
        _buses[handle] = this;
      }
      i2c_master_bus_handle_t _handle;
      i2c_master_bus_config_t _config;
      std::map<i2c_master_dev_handle_t, MasterDev*> _devices;
      mutable std::recursive_mutex _mutex;
      static std::map<i2c_master_bus_handle_t, MasterBus*> _buses;
      static std::mutex _busesMutex;
      friend class MasterDev;
    };

    class MasterDev : public log::Loggable {
     public:
      virtual ~MasterDev() {
        std::lock_guard lock(_bus->_mutex);
        if (_handle) {
          _bus->_devices.erase(_handle);
          //          logw("removing I2C device %x", _config.device_address);
          int attempts = 5;
          esp_err_t res;
          while (attempts-- > 0) {
            res = i2c_master_bus_wait_all_done(_bus->_handle, _timeout);
            if (res != ESP_OK)
              break;
            res = i2c_master_bus_rm_device(_handle);
            if (res == ESP_OK)
              break;
            delay(10);
          }
          ESP_ERROR_CHECK_WITHOUT_ABORT(res);
          _handle = nullptr;
        }
      }
      const char* name() const override {
        return _name.c_str();
      }
      i2c_master_dev_handle_t handle() const {
        return _handle;
      }
      // Hold this lock around any synchronous SDK call using handle(), or
      // around a sequence which must not interleave with other bus users.
      // Asynchronous SDK transfers are not supported by this wrapper.
      std::recursive_mutex& mutex() const {
        return _bus->mutex();
      }
      uint16_t address() const {
        return _config.device_address;
      }
      int getTimeout() const {
        std::lock_guard lock(_bus->_mutex);
        return _timeout;
      }
      void setTimeout(int ms) {
        std::lock_guard lock(_bus->_mutex);
        _timeout = ms;
      }
      int getRetries() const {
        std::lock_guard lock(_bus->_mutex);
        return _retries;
      }
      void setRetries(int retries) {
        std::lock_guard lock(_bus->_mutex);
        _retries = retries < 0 ? 0 : retries;
      }
      bool getRetryInvalidResponse() const {
        std::lock_guard lock(_bus->_mutex);
        return _retryInvalidResponse;
      }
      // Some IDF versions report bus timeouts as INVALID_RESPONSE. Retrying
      // that result can replay an accepted write, so opt in only for devices
      // whose operations are safe to repeat (for example GPIO registers).
      void setRetryInvalidResponse(bool retry) {
        std::lock_guard lock(_bus->_mutex);
        _retryInvalidResponse = retry;
      }
      Endian getEndianness() const {
        std::lock_guard lock(_bus->_mutex);
        return _endianness;
      }
      void setEndianness(Endian endianness) {
        std::lock_guard lock(_bus->_mutex);
        _endianness = endianness;
      }
      bool isResponsive() const {
        std::lock_guard lock(_bus->_mutex);
        return _commFailCount < 5 ||
               (_timeout > 0 &&
                millis() - _lastSuccessfulComm <
                    static_cast<uint64_t>(_timeout) * 100);
      }
      esp_err_t read(const void* out_data, size_t out_size, void* in_data,
                     size_t in_size)

      {
        std::lock_guard lock(_bus->_mutex);
        esp_err_t res = withRetries([&]() {
          if (out_data && out_size)
            return i2c_master_transmit_receive(
                _handle, (const uint8_t*)out_data, out_size, (uint8_t*)in_data,
                in_size, _timeout);
          return i2c_master_receive(_handle, (uint8_t*)in_data, in_size,
                                    _timeout);
        });
        return checkComm(res);
      }
      esp_err_t write(const void* out_reg, size_t out_reg_size,
                      const void* out_data, size_t out_size) {
        std::lock_guard lock(_bus->_mutex);
        esp_err_t res = withRetries([&]() {
          esp_err_t local = ESP_ERR_INVALID_ARG;
          if (out_reg && out_reg_size && out_data && out_size) {
            i2c_master_transmit_multi_buffer_info_t buf[2];
            buf[0].write_buffer = (uint8_t*)out_reg;
            buf[0].buffer_size = out_reg_size;
            buf[1].write_buffer = (uint8_t*)out_data;
            buf[1].buffer_size = out_size;
            local = i2c_master_multi_buffer_transmit(_handle, buf, 2, _timeout);
          } else if (out_reg && out_reg_size)
            local = i2c_master_transmit(_handle, (const uint8_t*)out_reg,
                                        out_reg_size, _timeout);
          else if (out_data && out_size)
            local = i2c_master_transmit(_handle, (const uint8_t*)out_data,
                                        out_size, _timeout);
          return local;
        });
        return checkComm(res);
      }
      inline esp_err_t read(uint8_t reg, void* in_data, size_t in_size) {
        return read(&reg, 1, in_data, in_size);
      }
      inline esp_err_t write(uint8_t reg, const void* out_data,
                             size_t out_size) {
        return write(&reg, 1, out_data, out_size);
      }
      inline esp_err_t read(uint8_t reg, uint8_t& value) {
        return read(&reg, 1, &value, 1);
      }
      inline esp_err_t write(uint8_t reg, uint8_t value) {
        return write(&reg, 1, &value, 1);
      }
      template <class T>
      esp_err_t read(uint8_t reg, T& value) {
        std::lock_guard lock(_bus->_mutex);
        T v;
        ESP_CHECK_RETURN(read(&reg, 1, &v, sizeof(T)));
        value = fromEndian(_endianness, v);
        return ESP_OK;
      }
      template <class T>
      esp_err_t write(uint8_t reg, const T value) {
        std::lock_guard lock(_bus->_mutex);
        T v = toEndian(_endianness, value);
        return write(&reg, 1, &v, sizeof(T));
      }

      static esp_err_t New(i2c_device_config_t config, MasterBus* bus,
                           MasterDev** dev) {
        if (!dev)
          return ESP_ERR_INVALID_ARG;
        *dev = nullptr;
        if (!bus)
          bus = MasterBus::getDefault();
        if (!bus)
          return ESP_FAIL;
        std::lock_guard lock(bus->_mutex);
        i2c_master_dev_handle_t handle;
        ESP_CHECK_RETURN(
            i2c_master_bus_add_device(bus->_handle, &config, &handle));
        *dev = new MasterDev(bus, config, handle);
        return ESP_OK;
      }
      static MasterDev* create(uint16_t addr, MasterBus* bus = nullptr) {
        i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                      .device_address = addr,
                                      .scl_speed_hz = 100000,
                                      .scl_wait_us = 0,
                                      .flags = {
                                          .disable_ack_check = 0,
                                      }};
        MasterDev* dev = nullptr;
        New(config, bus, &dev);
        return dev;
      }

     private:
      MasterBus* _bus;
      std::string _name;
      int _timeout = 30;
      int _retries = 3;
      bool _retryInvalidResponse = false;
      Endian _endianness = Endian::Big;
      const i2c_device_config_t _config;
      i2c_master_dev_handle_t _handle;
      unsigned long _lastCommAttempt = 0, _lastSuccessfulComm = 0;
      int _commFailCount = 0;
      MasterDev(MasterBus* bus, const i2c_device_config_t& config,
                i2c_master_dev_handle_t handle)
          : _bus(bus), _config(config), _handle(handle) {
        bus->_devices[handle] = this;
        _name = string_printf("I2C%d[0x%02" PRIx8 "]", _bus->_config.i2c_port,
                              _config.device_address);
      };

      esp_err_t checkComm(esp_err_t res) {
        auto now = millis();
        _lastCommAttempt = now;
        if (res == ESP_OK) {
          _lastSuccessfulComm = now;
          _commFailCount = 0;
        } else if (_commFailCount < 5)
          _commFailCount++;
        return res;
      }

      template <class F>
      esp_err_t withRetries(F&& fn) {
        // The caller holds the bus lock through the initial attempt, reset,
        // backoff and retries, so another device cannot transfer during reset.
        int retries_left = _retries;
        while (true) {
          auto res = fn();
          const bool retryable = res == ESP_ERR_TIMEOUT ||
                                 (_retryInvalidResponse &&
                                  res == ESP_ERR_INVALID_RESPONSE);
          if (!retryable || retries_left == 0)
            return res;
          --retries_left;
          auto resetResult = _bus->reset();
          if (resetResult != ESP_OK)
            return resetResult;
          const TickType_t ticks = pdMS_TO_TICKS(2);
          vTaskDelay(ticks ? ticks : 1);
        }
      }
    };

  }  // namespace i2c
}  // namespace esp32m
