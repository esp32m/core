#include "esp32m/bus/i2c/master.hpp"

namespace esp32m {
  namespace i2c {

    std::map<i2c_master_bus_handle_t, MasterBus*> MasterBus::_buses;
    std::mutex MasterBus::_busesMutex;

  }  // namespace i2c
}  // namespace esp32m