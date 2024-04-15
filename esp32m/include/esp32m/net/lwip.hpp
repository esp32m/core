#pragma once

#include <lwip/netif.h>

namespace esp32m {
  namespace lwip {

    const char* netif_name(const struct netif* nif, char* buf, size_t bufsize);

  }
}  // namespace esp32m