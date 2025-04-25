#include "esp32m/net/icmp.hpp"
#include <esp_task_wdt.h>
#include "esp32m/defs.hpp"
#include "esp32m/json.hpp"
#include "esp32m/net/net.hpp"
#include "lwip/inet_chksum.h"
#include "lwip/netdb.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/udp.h"

namespace esp32m {
  namespace net {

    const char *DefaultPingHost = "google.com";

    Ping::Ping() {
      _cbs = {
          .cb_args = this,
          .on_ping_success =
              [](esp_ping_handle_t h, void *args) {
                ((Ping *)args)->report(h, Status::Success);
              },
          .on_ping_timeout =
              [](esp_ping_handle_t h, void *args) {
                ((Ping *)args)->report(h, Status::Timeout);
              },
          .on_ping_end =
              [](esp_ping_handle_t h, void *args) {
                ((Ping *)args)->report(h, Status::End);
              },
      };
    }
    void Ping::report(esp_ping_handle_t h, Status s) {
      if (h != _handle)
        return;
      if (!_pendingResponse)
        return;
      switch (s) {
        case Status::Success: {
          //size_t size = JSON_OBJECT_SIZE(6) + Ipv6MaxChars;
          auto doc = new JsonDocument(); /* size */
          auto root = doc->to<JsonObject>();
          uint16_t seq;
          uint8_t ttl;
          uint32_t time;
          uint32_t bytes;
          ip_addr_t addr;
          esp_ping_get_profile(_handle, ESP_PING_PROF_SEQNO, &seq, sizeof(seq));
          esp_ping_get_profile(_handle, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
          esp_ping_get_profile(_handle, ESP_PING_PROF_IPADDR, &addr,
                               sizeof(addr));
          esp_ping_get_profile(_handle, ESP_PING_PROF_SIZE, &bytes,
                               sizeof(bytes));
          esp_ping_get_profile(_handle, ESP_PING_PROF_TIMEGAP, &time,
                               sizeof(time));
          json::to(root, "status", (int)s);
          json::to(root, "seq", seq);
          json::to(root, "ttl", ttl);
          json::to(root, "time", time);
          json::to(root, "bytes", bytes);
          json::to(root, "addr", addr);
          _pendingResponse->setPartial(true);
          _pendingResponse->setData(doc);
          _pendingResponse->publish();
        } break;
        case Status::Timeout: {
          // size_t size = JSON_OBJECT_SIZE(3) + Ipv6MaxChars;
          auto doc = new JsonDocument(); /* size */
          auto root = doc->to<JsonObject>();
          uint16_t seq;
          ip_addr_t addr;
          esp_ping_get_profile(_handle, ESP_PING_PROF_SEQNO, &seq, sizeof(seq));
          esp_ping_get_profile(_handle, ESP_PING_PROF_IPADDR, &addr,
                               sizeof(addr));
          json::to(root, "status", (int)s);
          json::to(root, "seq", seq);
          json::to(root, "addr", addr);
          _pendingResponse->setPartial(true);
          _pendingResponse->setData(doc);
          _pendingResponse->publish();
        } break;
        case Status::End: {
          // size_t size = JSON_OBJECT_SIZE(4);
          auto doc = new JsonDocument(); /* size */
          auto root = doc->to<JsonObject>();
          uint32_t tx;
          uint32_t rx;
          uint32_t time;
          esp_ping_get_profile(_handle, ESP_PING_PROF_REQUEST, &tx, sizeof(tx));
          esp_ping_get_profile(_handle, ESP_PING_PROF_REPLY, &rx, sizeof(rx));
          esp_ping_get_profile(_handle, ESP_PING_PROF_DURATION, &time,
                               sizeof(time));
          json::to(root, "status", (int)s);
          json::to(root, "tx", tx);
          json::to(root, "rx", rx);
          json::to(root, "time", time);
          _pendingResponse->setPartial(false);
          _pendingResponse->setData(doc);
          _pendingResponse->publish();
          freeResponse();
        } break;
        default:
          break;
      }
    }

    esp_err_t Ping::start() {
      if (_handle)
        ESP_CHECK_RETURN(stop());
      ip_addr_t target_addr;
      struct addrinfo hint;
      struct addrinfo *res = NULL;
      memset(&hint, 0, sizeof(hint));
      memset(&target_addr, 0, sizeof(target_addr));
      getaddrinfo(_host.size() ? _host.c_str() : DefaultPingHost, NULL, &hint,
                  &res);
      struct in_addr addr4 = ((struct sockaddr_in *)(res->ai_addr))->sin_addr;
      inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
      freeaddrinfo(res);
      _config.target_addr = target_addr;
      _config.task_stack_size = 4096;
      ESP_CHECK_RETURN(esp_ping_new_session(&_config, &_cbs, &_handle));
      ESP_CHECK_RETURN(esp_ping_start(_handle));
      return ESP_OK;
    }

    esp_err_t Ping::stop() {
      if (_handle) {
        ESP_CHECK_RETURN(esp_ping_stop(_handle));
        ESP_CHECK_RETURN(esp_ping_delete_session(_handle));
        _handle = nullptr;
      }
      return ESP_OK;
    }

    bool Ping::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("start")) {
        auto err = start();
        if (err != ESP_OK)
          req.respond(err);
        else {
          freeResponse();
          _pendingResponse = req.makeResponse();
        }
        return true;
      } else if (req.is("stop")) {
        auto err = stop();
        if (err != ESP_OK)
          req.respond(err);
        else
          req.respond();
        freeResponse();
        return true;
      }
      return false;
    }

    void Ping::freeResponse() {
      if (_pendingResponse) {
        delete _pendingResponse;
        _pendingResponse = nullptr;
      }
    }

    JsonDocument *Ping::getState(RequestContext &ctx) {
      //size_t size = JSON_OBJECT_SIZE(2);
      auto doc = new JsonDocument(); /* size */
      auto root = doc->to<JsonObject>();
      json::to(root, "running", 1);
      return doc;
    }

    JsonDocument *Ping::getConfig(RequestContext &ctx) {
      /*size_t size = JSON_OBJECT_SIZE(8) + JSON_STRING_SIZE(_host.size()) +
                    json::interfacesSize();*/
      auto doc = new JsonDocument(); /* size */
      auto root = doc->to<JsonObject>();
      json::interfacesTo(root);
      if (_host.size())
        json::to(root, "target", _host);
      else
        json::to(root, "target", DefaultPingHost);
      json::to(root, "count", _config.count);
      json::to(root, "interval", _config.interval_ms);
      json::to(root, "timeout", _config.timeout_ms);
      json::to(root, "size", _config.data_size);
      json::to(root, "tos", _config.tos);
      json::to(root, "ttl", _config.ttl);
      json::to(root, "iface", _config.interface);
      return doc;
    }

    bool Ping::setConfig(RequestContext &ctx) {
      JsonObjectConst obj = ctx.data.as<JsonObjectConst>();
      bool changed = false;
      json::from(obj["target"], _host, &changed);
      if (!_host.size()) {
        _host = DefaultPingHost;
        changed = true;
      }
      json::from(obj["count"], _config.count, &changed);
      json::from(obj["interval"], _config.interval_ms, &changed);
      json::from(obj["timeout"], _config.timeout_ms, &changed);
      json::from(obj["size"], _config.data_size, &changed);
      json::from(obj["tos"], _config.tos, &changed);
      json::from(obj["ttl"], _config.ttl, &changed);
      json::from(obj["iface"], _config.interface, &changed);
      return changed;
    }

    Ping &Ping::instance() {
      static Ping i;
      return i;
    }

    Ping *usePing() {
      return &Ping::instance();
    }

    namespace traceroute {

      struct packetdata {
        u_char seq;   /* sequence number of this packet */
        u_int8_t ttl; /* ttl packet left with */
        u_char pad[2];
        u_int32_t sec; /* time packet left */
        u_int32_t usec;
      } __packed;

      Session::Session(Config &config) : _conf(config) {}
      Session::~Session() {
        if (_sockSend >= 0 && _sockRecv != _sockSend)
          close(_sockSend);
        _sockSend = -1;
        if (_sockRecv >= 0)
          close(_sockRecv);
        _sockRecv = -1;
        if (_outpkt)
          free(_outpkt);
        _outpkt = nullptr;
      }

      esp_err_t Session::init(const char *dest) {
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = _conf.v6 ? PF_INET6 : PF_INET;
        hints.ai_socktype = SOCK_RAW;
        hints.ai_protocol = 0;
        hints.ai_flags = AI_CANONNAME;
        if (getaddrinfo(dest, NULL, &hints, &res))
          return ESP_FAIL;
        /* switch (res->ai_family) {
           case AF_INET:
           case AF_INET6:
             _to = (sockaddr *)calloc(1, res->ai_addrlen);
             break;
           default:
             _to = nullptr;
             break;
         }*/
        memcpy(&_to, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
        if (!_conf.ident)
          _conf.ident = (((uint32_t)this) & 0xffff) | 0x8000;
        int headerlen;
        _datalen = 0;
        switch (_to.a.sa_family) {
          case AF_INET:
            switch (_conf.proto) {
              case IPPROTO_UDP:
                headerlen = sizeof(udp_hdr) + sizeof(packetdata);
                break;
              case IPPROTO_ICMP:
                headerlen = sizeof(icmp_echo_hdr) + sizeof(packetdata);
                break;
              default:
                headerlen = sizeof(packetdata);
                break;
            }

            if (_datalen < 0 || _datalen > 65535 - headerlen)
              return ESP_ERR_NO_MEM;

            _datalen += headerlen;

            if (!(_outpkt = calloc(1, _datalen)))
              return ESP_ERR_NO_MEM;

            break;
          case AF_INET6:
            break;
          default:
            break;
        }
        _sockRecv = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (_conf.proto == IPPROTO_ICMP)
          _sockSend = _sockRecv;
        else
          _sockSend = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
        struct timeval timeout;
        timeout.tv_sec = _conf.timeout_ms / 1000;
        timeout.tv_usec = (_conf.timeout_ms % 1000) * 1000;
        setsockopt(_sockRecv, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout));
        setsockopt(_sockSend, IPPROTO_IP, IP_TOS, &_conf.tos,
                   sizeof(_conf.tos));
        if (_conf.interface) {
          struct ifreq iface;
          if (netif_index_to_name(_conf.interface, iface.ifr_name)) {
            setsockopt(_sockSend, SOL_SOCKET, SO_BINDTODEVICE, &iface,
                       sizeof(iface));
            if (_sockSend != _sockRecv)
              setsockopt(_sockRecv, SOL_SOCKET, SO_BINDTODEVICE, &iface,
                         sizeof(iface));
          }
        }

        _seq = 0;
        return ESP_OK;
      }

      void Session::build4(int ttl) {
        auto up = (udp_hdr *)(_outpkt);
        auto icmpp = (icmp_echo_hdr *)(_outpkt);
        packetdata *op;

        setsockopt(_sockSend, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));

        switch (_conf.proto) {
          case IPPROTO_ICMP:
            icmpp->type = ICMP_ECHO;
            icmpp->code = 0;
            icmpp->seqno = htons(_seq);
            icmpp->id = htons(_conf.ident);
            op = (struct packetdata *)(icmpp + 1);
            break;
          case IPPROTO_UDP:
            up->src = htons(_conf.ident);
            up->dest = htons(_conf.port + _seq);
            up->len = htons((uint16_t)(_datalen));
            up->chksum = 0;
            op = (struct packetdata *)(up + 1);
            break;
          default:
            op = (struct packetdata *)(_outpkt);
            break;
        }
        op->seq = _seq;
        op->ttl = ttl;
        timeval tv;
        gettimeofday(&tv, NULL);
        op->sec = htonl(tv.tv_sec);
        op->usec = htonl((tv.tv_usec) % 1000000);
        icmpp->chksum = 0;
        if (_conf.proto == IPPROTO_ICMP)
          icmpp->chksum = inet_chksum(icmpp, _datalen);
      }

      void Session::step() {
        int ttl = _conf.first_ttl + _seq / _conf.nprobes;
        if (ttl > _conf.max_ttl) {
          _complete = true;
          return;
        }
        memset(&_result, 0, sizeof(_result));
        switch (_to.a.sa_family) {
          case AF_INET:
            build4(ttl);
            break;
          case AF_INET6:
            // TODO
            break;
          default:
            break;
        }

        _result.seq = _seq;
        _result.row = _seq / _conf.nprobes;
        _result.ttls = ttl;
        _result.icmpc = -1;
        _result.ts = micros();
        auto i = sendto(_sockSend, _outpkt, _datalen, 0, &_to.a, _to.a.sa_len);
        if (i == -1 || i != _datalen)
          logw("sendto failed: %d!=%d", i, _datalen);
        // logi("sent %i bytes to %x", i, _to.in.sin_addr.s_addr);

        _seq++;

        iovec iov = {.iov_base = &_inbuf[0], .iov_len = sizeof(_inbuf)};
        msghdr rcvmsg = {.msg_name = &_from,
                         .msg_namelen = sizeof(_from),
                         .msg_iov = &iov,
                         .msg_iovlen = 1,
                         .msg_control = NULL,
                         .msg_controllen = 0,
                         .msg_flags = 0};
        int cc = recvmsg(_sockRecv, &rcvmsg, 0);
        if (cc == 0)
          return;

        int recv_seq = 0;
        bool gotThere = false;
        // log::system().dump(log::Level::Info, _inbuf, cc);
        switch (_to.a.sa_family) {
          case AF_INET: {
            _result.from.type = IPADDR_TYPE_V4;
            _result.from.u_addr.ip4.addr = _from.in.sin_addr.s_addr;
            auto ip = (ip_hdr *)_inbuf;
            auto hlen = IPH_HL_BYTES(ip);
            if (cc < hlen + 8 /*ICMP_MINLEN*/)
              return;
            _result.ttlr = IPH_TTL(ip);
            cc -= hlen;
            auto icp = (icmp_echo_hdr *)(_inbuf + hlen);
            auto type = icp->type;
            auto code = icp->code;
            if ((type == 11 /*ICMP_TIMXCEED*/ &&
                 code == 0 /*ICMP_TIMXCEED_INTRANS*/) ||
                type == 3 /*ICMP_UNREACH*/ || type == 0 /*ICMP_ECHOREPLY*/) {
              auto hip = (ip_hdr *)(icp + 1);
              hlen = IPH_HL_BYTES(hip);
              icmp_echo_hdr *icmpp;
              udp_hdr *up;
              switch (_conf.proto) {
                case IPPROTO_ICMP:
                  if (type == 0 /*ICMP_ECHOREPLY*/ &&
                      icp->id == htons(_conf.ident)) {
                    recv_seq = ntohs(icp->seqno);
                    gotThere = true;
                    break; /* we got there */
                  }
                  icmpp = (icmp_echo_hdr *)((u_char *)hip + hlen);
                  if (hlen + 8 <= cc && IPH_PROTO(hip) == IPPROTO_ICMP &&
                      icmpp->id == htons(_conf.ident)) {
                    recv_seq = ntohs(icmpp->seqno);
                    if (type != 11 /*ICMP_TIMXCEED*/)
                      _result.icmpc = code;
                  }
                  break;

                case IPPROTO_UDP:
                  up = (udp_hdr *)((u_char *)hip + hlen);
                  if (hlen + 12 <= cc && IPH_PROTO(hip) == _conf.proto &&
                      up->src == htons(_conf.ident)) {
                    recv_seq = ntohs(up->dest) - _conf.port;
                    if (type != 11 /*ICMP_TIMXCEED*/)
                      _result.icmpc = code;
                  }
                  break;
                default:
                  /* this is some odd, user specified proto,
                   * how do we check it?
                   */
                  if (IPH_PROTO(hip) == _conf.proto)
                    if (type != 11 /*ICMP_TIMXCEED*/)
                      _result.icmpc = code;
                  break;
              }
            }
          } break;
          case AF_INET6:
            // TODO
            break;
          default:
            break;
        }

        /* skip corrupt sequence number */
        if (recv_seq < 0 || recv_seq >= _conf.max_ttl * _conf.nprobes)
          return;
        _result.tr = micros();
        if (_result.icmpc != -1) {
          if (_result.icmpc == 2 /*ICMP_UNREACH_PORT*/ ||
              _result.icmpc == 3 /*ICMP_UNREACH_PROTOCOL*/)
            gotThere = true;
          else
            _result.unreach = true;
        }
        if (gotThere && _seq % _conf.nprobes == 0)
          _complete = true;
        // logi("received %d bytes from %x", cc, _from.in.sin_addr.s_addr);
      }

    }  // namespace traceroute

    Traceroute::Traceroute() {}

    bool Traceroute::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("start")) {
        auto err = start();
        if (err != ESP_OK)
          req.respond(err);
        else {
          freeResponse();
          _pendingResponse = req.makeResponse();
        }
        return true;
      } else if (req.is("stop")) {
        auto err = stop();
        if (err != ESP_OK)
          req.respond(err);
        else
          req.respond();
        freeResponse();
        return true;
      }
      return false;
    }

    void Traceroute::freeResponse() {
      if (_pendingResponse) {
        delete _pendingResponse;
        _pendingResponse = nullptr;
      }
    }

    esp_err_t Traceroute::start() {
      ESP_CHECK_RETURN(stop());
      _stopped = false;
      xTaskCreate([](void *self) { ((Traceroute *)self)->run(); }, "m/trrt",
                  4096, this, tskIDLE_PRIORITY + 1, &_task);
      return ESP_OK;
    }
    esp_err_t Traceroute::stop() {
      while (_task) {
        _stopped = true;
        xTaskNotifyGive(_task);
        sleep(1);
      }
      _stopped = true;
      return ESP_OK;
    }

    void Traceroute::run() {
      auto session = new traceroute::Session(_conf);
      auto err = session->init(_host.size() ? _host.c_str() : DefaultPingHost);
      if (err != ESP_OK) {
        if (_pendingResponse) {
          _pendingResponse->setError(err);
          _pendingResponse->publish();
        }
      } else {
        esp_task_wdt_add(NULL);
        while (!_stopped) {
          esp_task_wdt_reset();
          session->step();
          if (_pendingResponse) {
            auto r = session->result();
           //size_t size = JSON_OBJECT_SIZE(7) + Ipv6MaxChars;
            auto doc = new JsonDocument(); /* size */
            auto root = doc->to<JsonObject>();
            json::to(root, "ip", r->from);
            json::to(root, "seq", r->seq);
            json::to(root, "row", r->row);
            json::to(root, "ttls", r->ttls);
            json::to(root, "ttlr", r->ttlr, 0);
            auto tus = r->tr - r->ts;
            if (tus > 0 && tus / 1000 < _conf.timeout_ms)
              json::to(root, "tus", tus);
            json::to(root, "icmpc", r->icmpc, -1);
            _pendingResponse->setData(doc);
            _pendingResponse->setPartial(!session->isComplete());
            _pendingResponse->publish();
          }
          if (session->isComplete())
            _stopped = true;
          if (!_stopped)
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        }
        esp_task_wdt_delete(NULL);
      }
      freeResponse();
      delete session;
      _task = nullptr;
      vTaskDelete(NULL);
    }

    JsonDocument *Traceroute::getState(RequestContext &ctx) {
      //size_t size = JSON_OBJECT_SIZE(1);
      auto doc = new JsonDocument(); /* size */
      auto root = doc->to<JsonObject>();
      json::to(root, "running", !_stopped);
      return doc;
    }

    JsonDocument *Traceroute::getConfig(RequestContext &ctx) {
      /*size_t size = JSON_OBJECT_SIZE(8) + JSON_STRING_SIZE(_host.size()) +
                    json::interfacesSize();*/
      auto doc = new JsonDocument(); /* size */
      auto root = doc->to<JsonObject>();
      json::interfacesTo(root);
      if (_host.size())
        json::to(root, "target", _host);
      else
        json::to(root, "target", DefaultPingHost);
      json::to(root, "timeout", _conf.timeout_ms);
      json::to(root, "proto", _conf.proto);
      json::to(root, "iface", _conf.interface);
      return doc;
    }

    bool Traceroute::setConfig(RequestContext &ctx) {
      auto obj=ctx.data.as<JsonObjectConst>();
      bool changed = false;
      json::from(obj["target"], _host, &changed);
      if (!_host.size()) {
        _host = DefaultPingHost;
        changed = true;
      }
      json::from(obj["timeout"], _conf.timeout_ms, &changed);
      json::from(obj["proto"], _conf.proto, &changed);
      json::from(obj["iface"], _conf.interface, &changed);
      return changed;
    }

    Traceroute &Traceroute::instance() {
      static Traceroute i;
      return i;
    }

    Traceroute *useTraceroute() {
      return &Traceroute::instance();
    }
  }  // namespace net
}  // namespace esp32m