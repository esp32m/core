#include "esp32m/net/lwip.hpp"

namespace esp32m {
  namespace lwip {

    const char* netif_name(const struct netif* nif, char* buf, size_t bufsize) {
      if (bufsize < 4)
        return nullptr;
      buf[0] = nif->name[0];
      buf[1] = nif->name[1];
      buf[2] = nif->num + '0';
      buf[3] = 0;
      return buf;
    }

  }  // namespace lwip
}  // namespace esp32m