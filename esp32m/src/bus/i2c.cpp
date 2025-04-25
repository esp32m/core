#include "esp32m/bus/i2c/master.hpp"

namespace esp32m {
  namespace i2c {

    std::map<i2c_master_bus_handle_t, MasterBus*> MasterBus::_buses;
    std::mutex MasterBus::_busesMutex;

    esp_err_t MasterBus::find(uint16_t address, MasterDev** dev) {
      for (auto & [_, d] : _devices)
        if (d->address() == address) {
          if (dev)
            *dev = d;
          return ESP_OK;
        }
      return ESP_ERR_NOT_FOUND;
    }

  }  // namespace i2c
}  // namespace esp32m