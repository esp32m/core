#include <lwip/opt.h>

#if LWIP_IPV6 && \
    LWIP_ND6 /* don't build if not configured for use in lwipopts.h */

#  include "esp32m/debug/lwip/nd6.hpp"
#  include "esp32m/net/lwip.hpp"

#  include <lwip/priv/nd6_priv.h>
#  include <lwip/sockets.h>

#  include <math.h>
#  include <string>
#  include <vector>

namespace esp32m {

  namespace debug {
    namespace lwip {

      class Neigh {
       public:
        Neigh(const nd6_neighbor_cache_entry& entry) {
          char buf[40];
          _nexthop = (char*)inet_ntop(AF_INET6, entry.next_hop_address.addr,
                                      buf, sizeof(buf));
          auto hwl = std::min((size_t)entry.netif->hwaddr_len, sizeof(buf) / 3);
          char* pbuf = buf;
          for (int i = 0; i < hwl; i++) {
            if (pbuf != buf)
              *(pbuf - 1) = ':';
            sprintf(pbuf, "%02x", entry.lladdr[i]);
            pbuf += 3;
          }
          _lladdr = buf;
          esp32m::lwip::netif_name(entry.netif, _ifname, sizeof(_ifname));
          _state = entry.state;
          _isrouter = entry.isrouter;
          _counter = entry.counter.reachable_time;
          if (_isrouter)
            for (int i = 0; i < LWIP_ND6_NUM_ROUTERS; i++)
              if (default_router_list[i].neighbor_entry == &entry) {
                _rit = default_router_list[i].invalidation_timer;
                _rflags = default_router_list[i].flags;
              }
        }
        size_t jsonSize() {
          auto s = JSON_ARRAY_SIZE(6) + JSON_STRING_SIZE(_nexthop.size()) +
                   JSON_STRING_SIZE(_lladdr.size()) + JSON_STRING_SIZE(3);
          if (_isrouter)
            s += JSON_ARRAY_SIZE(2);
          return s;
        }
        void toJson(JsonArray a) {
          a = a.createNestedArray();
          a.add(_ifname);
          a.add(_lladdr);
          a.add(_nexthop);
          a.add(_state);
          a.add(_isrouter);
          a.add(_counter);
          if (_isrouter) {
            a.add(_rit);
            a.add(_rflags);
          }
        }

       private:
        std::string _nexthop, _lladdr;
        char _ifname[4];
        u8_t _state;
        bool _isrouter;
        u32_t _counter;
        u32_t _rit = 0; /* in seconds */
        u8_t _rflags = 0;
      };

      class Dest {
       public:
        Dest(const nd6_destination_cache_entry& entry) {
          char buf[40];
          _dest = (char*)inet_ntop(AF_INET6, entry.destination_addr.addr, buf,
                                   sizeof(buf));
          _nexthop = (char*)inet_ntop(AF_INET6, entry.next_hop_addr.addr, buf,
                                      sizeof(buf));
          _pmtu = entry.pmtu;
          _age = entry.age;
        }
        size_t jsonSize() {
          return JSON_ARRAY_SIZE(4) + JSON_STRING_SIZE(_nexthop.size()) +
                 JSON_STRING_SIZE(_dest.size());
        }
        void toJson(JsonArray a) {
          a = a.createNestedArray();
          a.add(_dest);
          a.add(_nexthop);
          a.add(_pmtu);
          a.add(_age);
        }

       private:
        std::string _dest, _nexthop;
        u16_t _pmtu;
        u32_t _age;
      };

      class Prefix {
       public:
        Prefix(nd6_prefix_list_entry& entry) {
          char buf[40];
          _prefix =
              (char*)inet_ntop(AF_INET6, entry.prefix.addr, buf, sizeof(buf));
          esp32m::lwip::netif_name(entry.netif, _ifname, sizeof(_ifname));
          _it = entry.invalidation_timer;
        }

        size_t jsonSize() {
          return JSON_ARRAY_SIZE(3) + JSON_STRING_SIZE(_prefix.size()) +
                 JSON_STRING_SIZE(3);
        }
        void toJson(JsonArray a) {
          a = a.createNestedArray();
          a.add(_prefix);
          a.add(_ifname);
          a.add(_it);
        }

       private:
        std::string _prefix;
        char _ifname[4];
        u32_t _it;
      };

      DynamicJsonDocument* Nd6::getState(const JsonVariantConst args) {
        size_t size = JSON_OBJECT_SIZE(3);
        std::vector<Neigh> neighs;
        for (int i = 0; i < LWIP_ND6_NUM_NEIGHBORS; i++)
          if (neighbor_cache[i].state != ND6_NO_ENTRY) {
            neighs.emplace_back(neighbor_cache[i]);
            size += neighs.back().jsonSize();
          }
        std::vector<Dest> dests;
        for (int i = 0; i < LWIP_ND6_NUM_DESTINATIONS; i++)
          if (!ip6_addr_isany(&(destination_cache[i].destination_addr))) {
            dests.emplace_back(destination_cache[i]);
            size += dests.back().jsonSize();
          }
        std::vector<Prefix> prefixes;
        for (int i = 0; i < LWIP_ND6_NUM_PREFIXES; i++)
          if (prefix_list[i].netif) {
            prefixes.emplace_back(prefix_list[i]);
            size += prefixes.back().jsonSize();
          }

        size += JSON_ARRAY_SIZE(neighs.size()) + JSON_ARRAY_SIZE(dests.size()) +
                JSON_ARRAY_SIZE(prefixes.size());
/*        for (auto& neigh : neighs) size += neigh.jsonSize();
        for (auto& dest : dests) size += dest.jsonSize();
        for (auto& prefix : prefixes) size += prefix.jsonSize();*/
        DynamicJsonDocument* doc = new DynamicJsonDocument(size);
        auto root = doc->to<JsonObject>();
        auto a = root.createNestedArray("neighs");
        for (auto& neigh : neighs) neigh.toJson(a);
        a = root.createNestedArray("dests");
        for (auto& dest : dests) dest.toJson(a);
        a = root.createNestedArray("prefixes");
        for (auto& prefix : prefixes) prefix.toJson(a);

        return doc;
      }

      Nd6* useNd6() {
        return &Nd6::instance();
      }

    }  // namespace lwip
  }    // namespace debug
}  // namespace esp32m

#endif