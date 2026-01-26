#include "esp32m/net/term/rfc2217.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      static constexpr unsigned kListenPort = 3333;

      RFC2217 &RFC2217::instance() {
        static RFC2217 i;
        return i;
      }

      RFC2217::RFC2217() {
        Rfc2217UartServerConfig cfg{};
        cfg.tcpPort = kListenPort;
        cfg.ownerName = "rfc2217";

        _server = std::make_unique<Rfc2217UartServer>(cfg);
        _server->start();
      }

    }  // namespace term
  }  // namespace io
}  // namespace esp32m
