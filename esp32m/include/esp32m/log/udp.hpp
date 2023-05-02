#pragma once

#include <lwip/sockets.h>

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace log {

    class Udp : public LogAppender {
     public:
      enum Format { Text, Syslog };
      Udp(const char *host = nullptr, uint16_t port = 514);
      Udp(const Udp &) = delete;
      ~Udp();
      Format format() {
        return _format;
      }
      void setMode(Format format) {
        _format = format;
      }
      std::string getHost() {
        return _host;
      }
      void setHost(const char *host);
      void enable(bool enabled) {
        _enabled = enabled;
      }
      bool isEnabled() const {
        return _enabled;
      }

     protected:
      virtual bool append(const LogMessage *message);

     private:
      Format _format;
      std::string _host;
      bool _enabled = false;
      struct sockaddr_in _addr;
      int _fd;
    };
  }  // namespace log
}  // namespace esp32m