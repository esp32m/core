#include "esp32m/dev/em/em.hpp"

namespace esp32m {
  namespace dev {
    namespace em {

      const char *_indNames[] = {
          "voltage",
          "current",
          "power",
          "power-apparent",
          "power-reactive",
          "power-factor",
          "angle",
          "frequency",
          "energy-active-imp",
          "energy-active-exp",
          "energy-reactive-imp",
          "energy-reactive-exp",
          "energy-active-total",
          "energy-reactive-total",
      };

      const char *indicatorName(Indicator ind) {
        auto i = static_cast<int>(ind);
        if (i < 0 || ind >= Indicator::MAX)
          return nullptr;
        return _indNames[i];
      }
    }  // namespace em
  }    // namespace dev
}  // namespace esp32m