#pragma once

#include "esp32m/app.hpp"
#include "esp32m/events/response.hpp"

#include "apps/ping/ping_sock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace esp32m {

  namespace net {
    class Ping : public AppObject {
     public:
      Ping(const Ping &) = delete;
      static Ping &instance();
      const char *name() const override {
        return "ping";
      }
      esp_err_t start();
      esp_err_t stop();

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(RequestContext &ctx) override;
      bool handleRequest(Request &req) override;

     private:
      enum class Status { Success, Timeout, End };
      Ping();
      esp_ping_callbacks_t _cbs;
      Response *_pendingResponse = nullptr;
      esp_ping_handle_t _handle = nullptr;
      std::string _host;
      esp_ping_config_t _config = ESP_PING_DEFAULT_CONFIG();
      void report(esp_ping_handle_t h, Status s);
      void freeResponse();
    };

    namespace traceroute {
      struct Config {
        bool v6;
        int first_ttl;   /* Set the first TTL or hop limit */
        uint8_t max_ttl; /* Set the maximum TTL / hop limit */
        int nprobes;
        int tos;
        int timeout_ms;
        uint16_t ident;
        uint8_t proto;
        uint16_t port; /* start udp dest port */
        uint32_t interface;
      };
      union Sockaddr {
        sockaddr a;
        sockaddr_in in;
        sockaddr_in6 in6;
      };
      struct Result {
        ip_addr_t from;
        int seq;
        int row;
        int ttls, ttlr;
        unsigned long ts, tr;
        int icmpc;
        bool unreach;
      };
      class Session {
       public:
        Session(Config &config);
        ~Session();
        esp_err_t init(const char *dest);
        void done();
        void build4(int ttl);
        void step();
        const Result *result() {
          return &_result;
        }
        bool isComplete() {
          return _complete;
        }

       private:
        Config _conf;
        Result _result;
        bool _complete = false;
        void *_outpkt = nullptr;
        int _datalen = 0;
        int _sockSend = -1, _sockRecv = -1;
        int _seq;
        Sockaddr _to = {}, _from = {};
        uint8_t _inbuf[64];
      };
    }  // namespace traceroute

    class Traceroute : public AppObject {
     public:
      Traceroute(const Traceroute &) = delete;
      static Traceroute &instance();
      const char *name() const override {
        return "traceroute";
      }
      esp_err_t start();
      esp_err_t stop();

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(RequestContext &ctx) override;
      bool handleRequest(Request &req) override;

     private:
      Traceroute();
      bool _stopped = false;
      Response *_pendingResponse = nullptr;
      TaskHandle_t _task = nullptr;
      std::string _host;
      traceroute::Config _conf = {
          .v6 = false,
          .first_ttl = 1,
          .max_ttl = 64, /* Set the maximum TTL / hop limit */
          .nprobes = 3,
          .tos = 0,
          .timeout_ms = 3000,
          .ident = 0,
          .proto = IPPROTO_ICMP,  // IPPROTO_UDP,
          .port = 32768 + 666,    /* start udp dest port */
          .interface = 0};
      void run();
      void freeResponse();
    };

    Ping *usePing();
    Traceroute *useTraceroute();
  }  // namespace net
}  // namespace esp32m