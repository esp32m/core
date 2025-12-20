#include "esp32m/dev/tm601awlcor.hpp"
namespace esp32m {
  namespace dev {
    Tm601awlcor* useTm601awlcor(uint8_t addr, const char* name) {
      return new Tm601awlcor(i2c::MasterDev::create(addr), name);
    }
  }  // namespace dev
}  // namespace esp32m
