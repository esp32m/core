#pragma once

#include <lwip/opt.h>

#if LWIP_IPV6 && LWIP_ND6 /* don't build if not configured for use in lwipopts.h */

#include "esp32m/app.hpp"

namespace esp32m {

  namespace debug {
    namespace lwip {

      class Nd6 : public AppObject {
       public:
        Nd6(const Nd6 &) = delete;
        static Nd6 &instance() {
          static Nd6 i;
          return i;
        }
        const char *name() const override {
          return "lwip-nd6";
        }

       protected:
        DynamicJsonDocument *getState(const JsonVariantConst args) override;

       private:
        Nd6() {}
      };

      Nd6 *useNd6();
    }  // namespace lwip
  }    // namespace debug

}  // namespace esp32m

#endif