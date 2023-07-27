#pragma once

#include <esp_netif_types.h>
#include <lwip/sockets.h>

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace net {
    class CaptiveDns : public log::SimpleLoggable {
     public:
      CaptiveDns(esp_ip4_addr_t ip);
      ~CaptiveDns();
      void enable(bool v);

     private:
      esp_ip4_addr_t _ip;
      bool _enabled = false;
      int _sockFd = 0;
      TaskHandle_t _task;
      void run();
      void recv(struct sockaddr_in *premote_addr, char *pusrdata,
                unsigned short length);
    };
  }  // namespace net
}  // namespace esp32m