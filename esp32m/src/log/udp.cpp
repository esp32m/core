#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <esp_netif.h>
#include <lwip/dns.h>
#include <lwip/netdb.h>
#include <string.h>

#include "esp32m/app.hpp"
#include "esp32m/events.hpp"
#include "esp32m/log/udp.hpp"
#include "esp32m/net/ip_event.hpp"
#include "esp32m/net/ota.hpp"
#include "esp32m/net/wifi.hpp"
namespace esp32m {

  namespace log {
    Udp::Udp(const char *host, uint16_t port) : _host(host), _fd(-1) {
      memset(&_addr, 0, sizeof(_addr));
      _addr.sin_family = AF_INET;
      _addr.sin_port = htons(port);
      if (host)
        inet_aton(host, &_addr.sin_addr.s_addr);
      _format = port == 514 ? Format::Syslog : Format::Text;
      if (_addr.sin_addr.s_addr == 0)
        EventManager::instance().subscribe([this](Event &ev) {
          IpEvent *ip;
          if (IpEvent::is(ev, &ip))
            switch (ip->event()) {
              case IP_EVENT_STA_LOST_IP:
                _addr.sin_addr.s_addr = 0;
                break;
              default:
                break;
            }
        });
    }

    Udp::~Udp() {
      if (_fd >= 0) {
        shutdown(_fd, 2);
        close(_fd);
        _fd = -1;
      }
    }

    const uint8_t SyslogSeverity[] = {5, 5, 3, 4, 6, 7, 7};

    bool Udp::append(const LogMessage *message) {
      if (!xPortCanYield())  // called from ISR
        return false;
      if (net::ota::isRunning())
        return false;
      if (!net::Wifi::instance().isConnected())
        return false;
      if (!_addr.sin_addr.s_addr) {
        const char *host = _host;
        if (!host)
          host = "syslog.lan";
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *res;
        int err = getaddrinfo(host, NULL, &hints, &res);
        if (err != 0 || res == NULL)
          return false;
        memcpy(&_addr.sin_addr,
               &((struct sockaddr_in *)(res->ai_addr))->sin_addr,
               sizeof(_addr.sin_addr));
        freeaddrinfo(res);
      }
      if (!_addr.sin_addr.s_addr)
        _addr.sin_addr.s_addr = net::Wifi::instance().staIp().gw.addr;
      if (!_addr.sin_addr.s_addr)
        return false;
      if (!message)
        return true;
      static char eol = '\n';
      if (_fd < 0) {
        struct timeval send_timeout = {1, 0};
        _fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (_fd >= 0)
          setsockopt(_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&send_timeout,
                     sizeof(send_timeout));
        else
          return false;
      }
      switch (_format) {
        case Format::Text: {
          auto formatter = log::formatter();
          auto msg = formatter(message);
          if (!msg)
            return true;
          auto len = strlen(msg);
          auto mptr = msg;
          while (len) {
            auto result = sendto(_fd, mptr, len, 0, (struct sockaddr *)&_addr,
                                 sizeof(_addr));
            if (result < 0) {
              free(msg);
              return false;
            }
            len -= result;
            mptr += result;
          }
          free(msg);
          return sendto(_fd, &eol, sizeof(eol), 0, (struct sockaddr *)&_addr,
                        sizeof(_addr)) == sizeof(eol);
        }
        case Format::Syslog:
          // https://tools.ietf.org/html/rfc5424
          int pri = 3 /*system daemons*/ * 8 + SyslogSeverity[message->level()];
          char strftime_buf[4 /* YEAR */ + 1 /* - */ + 2 /* MONTH */ +
                            1 /* - */ + 2 /* DAY */ + 1 /* T */ + 2 /* HOUR */ +
                            1 /* : */ + 2 /* MINUTE */ + 1 /* : */ +
                            2 /* SECOND */ + 1 /*NULL*/];
          auto stamp = message->stamp();
          struct tm timeinfo;
          auto neg = stamp < 0;
          if (neg)
            stamp = -stamp;
          time_t now = stamp / 1000;
          gmtime_r(&now, &timeinfo);
          if (!neg)
            timeinfo.tm_year = 0;
          strftime(strftime_buf, sizeof(strftime_buf), "%FT%T", &timeinfo);
          const char *hostname = App::instance().name();
          const char *name = message->name();
          auto ms = 1 /* < */ + 3 /* PRIVAL */ + 1 /* > */ + 1 /* version */ +
                    1 /* SP */ + strlen(strftime_buf) + 1 /* . */ + 4 /* MS */ +
                    1 /* Z */ + 1 /* SP */ + strlen(hostname) + 1 /* SP */ +
                    strlen(name) + 1 /* SP */ + 1 + /* PROCID */ +1 /*SP*/ + 1 +
                    /* MSGID */ +1 /* SP */ + 1 +
                    /* STRUCTURED-DATA */ +1 /* SP */ +
                    message->message_size() + 1 /*NULL*/;
          char *buf = (char *)malloc(ms);
          if (!buf)
            return true;
          sprintf(buf, "<%d>1 %s.%04dZ %s %s - - - %s", pri, strftime_buf,
                  (int)(stamp % 1000), hostname, name, message->message());
          auto result = sendto(_fd, buf, strlen(buf), 0,
                               (struct sockaddr *)&_addr, sizeof(_addr));
          if (result == EMSGSIZE) {
            sprintf(buf, "<%d>1 %s.%04dZ %s %s - - - %s", pri, strftime_buf,
                    (int)(stamp % 1000), hostname, name, "message too long!");
            result = sendto(_fd, buf, strlen(buf), 0, (struct sockaddr *)&_addr,
                            sizeof(_addr));
          }
          free(buf);
          return (result >= 0);
      }
      return true;
    }
  }  // namespace log
}  // namespace esp32m
