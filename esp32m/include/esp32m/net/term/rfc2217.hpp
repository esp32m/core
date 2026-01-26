#pragma once

#include <memory>

#include "esp32m/app.hpp"

#include "esp32m/net/term/rfc2217_uart_server.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      class RFC2217 : public AppObject {
       public:
        RFC2217(const RFC2217 &) = delete;
        RFC2217 &operator=(const RFC2217 &) = delete;

        static RFC2217 &instance();

        const char *name() const override {
          return "rfc2217";
        }

       private:
        RFC2217();

        std::unique_ptr<Rfc2217UartServer> _server;
      };

    }  // namespace term
  }  // namespace io
}  // namespace esp32m
