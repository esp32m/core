#include "esp32m/dev/ahtxx.hpp"

namespace esp32m {

  namespace ahtxx {


  }  // namespace ahtxx

  namespace dev {
    Ahtxx *useAhtxx(uint8_t addr, const char *name) {
      return new Ahtxx(i2c::MasterDev::create(addr), name);
    }
  }  // namespace dev
}  // namespace esp32m