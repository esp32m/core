#include "esp32m/bus/i2c.hpp"

namespace esp32m {
  namespace i2c {
    const int DevTimeout = 1000;

    std::mutex _mutexesLock;
    std::map<uint32_t, Mutex> _mutexes;

    inline static uint32_t mutex_key(uint8_t addr, i2c_port_t port, int sda,
                                     int scl) {
      return port | (sda << 8) | (scl << 16) | (addr << 24);
    }

    MutexRef::MutexRef(uint8_t addr, i2c_port_t port, int sda, int scl)
        : _key(mutex_key(addr, port, sda, scl)) {
      std::lock_guard guard(i2c::_mutexesLock);
      _mutexes[_key]._refs++;
    }

    MutexRef::~MutexRef() {
      std::lock_guard guard(i2c::_mutexesLock);
      Mutex &m = _mutexes[_key];
      m._refs--;
      if (m._refs == 0)
        _mutexes.erase(_key);
    }

    std::mutex &MutexRef::mutex() {
      std::lock_guard guard(i2c::_mutexesLock);
      Mutex &m = _mutexes[_key];
      return m._mutex;
    }

    struct Port {
      bool installed;
      i2c_config_t cfg;
      std::mutex mutex;
    };

    Port Ports[I2C_NUM_MAX] = {};

  }  // namespace i2c

  I2C::I2C(uint8_t addr, i2c_port_t port, gpio_num_t sda, gpio_num_t scl,
           uint32_t clk_speed)
      : _port(port),
        _cfg{.mode = I2C_MODE_MASTER,
             .sda_io_num = (int)sda,
             .scl_io_num = (int)scl,
             .sda_pullup_en = GPIO_PULLUP_ENABLE,
             .scl_pullup_en = GPIO_PULLUP_ENABLE,
             .master = {.clk_speed = clk_speed},
             .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL} {
    setAddr(addr);
  }

  void I2C::setAddr(uint8_t addr) {
    if (addr == _addr && _mutex)  // make sure _mutex gets initialized even if
                                  // someone tries to create I2C with addr==0
      return;
    _addr = addr;
    _mutex.reset(
        new i2c::MutexRef(addr, _port, _cfg.sda_io_num, _cfg.scl_io_num));
  }

  inline static bool cfg_equal(const i2c_config_t &a, const i2c_config_t &b) {
    return a.scl_io_num == b.scl_io_num && a.sda_io_num == b.sda_io_num &&
           a.master.clk_speed == b.master.clk_speed &&
           a.scl_pullup_en == b.scl_pullup_en &&
           a.sda_pullup_en == b.sda_pullup_en;
  }

  esp_err_t I2C::sync() {
    auto &p = i2c::Ports[_port];
    if (!cfg_equal(_cfg, p.cfg)) {
      i2c_config_t temp = _cfg;
      temp.mode = I2C_MODE_MASTER;
      if (p.installed)
        i2c_driver_delete(_port);
      ESP_CHECK_RETURN(i2c_param_config(_port, &temp));
      ESP_CHECK_RETURN(i2c_driver_install(_port, temp.mode, 0, 0, 0));
      p.installed = true;
      p.cfg = temp;
    }
    return ESP_OK;
  }

  esp_err_t I2C::read(const void *out_data, size_t out_size, void *in_data,
                      size_t in_size) {
    if (!in_data || !in_size)
      return ESP_ERR_INVALID_ARG;
    std::lock_guard lock(i2c::Ports[_port].mutex);
    if (!ready())
      return _err;
    esp_err_t res = sync();
    if (res == ESP_OK) {
      i2c_cmd_handle_t cmd = i2c_cmd_link_create();
      if (out_data && out_size) {
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, _addr << 1, true);
        i2c_master_write(cmd, (uint8_t *)out_data, out_size, true);
      }
      i2c_master_start(cmd);
      i2c_master_write_byte(cmd, (_addr << 1) | 1, true);
      i2c_master_read(cmd, (uint8_t *)in_data, in_size, I2C_MASTER_LAST_NACK);
      i2c_master_stop(cmd);
      res = ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_cmd_begin(
          _port, cmd, i2c::DevTimeout / portTICK_PERIOD_MS));
      i2c_cmd_link_delete(cmd);
    }
    _err = res;
    if (res != ESP_OK)
      _errStamp = millis();
    return res;
  }

  esp_err_t I2C::write(const void *out_reg, size_t out_reg_size,
                       const void *out_data, size_t out_size) {
    std::lock_guard lock(i2c::Ports[_port].mutex);
    if (!ready())
      return _err;
    esp_err_t res = sync();
    if (res == ESP_OK) {
      i2c_cmd_handle_t cmd = i2c_cmd_link_create();
      i2c_master_start(cmd);
      i2c_master_write_byte(cmd, _addr << 1, true);
      if (out_reg && out_reg_size)
        i2c_master_write(cmd, (uint8_t *)out_reg, out_reg_size, true);
      if (out_data && out_size)
        i2c_master_write(cmd, (uint8_t *)out_data, out_size, true);
      i2c_master_stop(cmd);
      res = ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_cmd_begin(
          _port, cmd, i2c::DevTimeout / portTICK_PERIOD_MS));
      i2c_cmd_link_delete(cmd);
    }
    _err = res;
    if (res != ESP_OK)
      _errStamp = millis();
    return res;
  }

}  // namespace esp32m
