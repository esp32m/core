#pragma once

#include <lwip/opt.h>

#if LWIP_IPV6 && LWIP_IPV6_MLD  /* don't build if not configured for use in lwipopts.h */

#include "esp32m/app.hpp"

namespace esp32m {

  namespace debug {
    namespace lwip {

      class Mld6 : public AppObject {
       public:
        Mld6(const Mld6 &) = delete;
        static Mld6 &instance() {
          static Mld6 i;
          return i;
        }
        const char *name() const override {
          return "lwip-mld6";
        }

       protected:
        JsonDocument *getState(RequestContext &ctx) override;

       private:
        Mld6() {}
      };

      Mld6 *useMld6();
    }  // namespace lwip
  }    // namespace debug

}  // namespace esp32m

#endif