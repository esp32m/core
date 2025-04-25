#include <lwip/opt.h>

#if LWIP_IPV6 && \
    LWIP_IPV6_MLD /* don't build if not configured for use in lwipopts.h */

#  include "esp32m/debug/lwip/mld6.hpp"
#  include "esp32m/net/lwip.hpp"

#  include <lwip/mld6.h>

namespace esp32m {

  namespace debug {
    namespace lwip {

      class Group {
       public:
        Group(const mld_group& entry) {
          char buf[40];
          _addr = (char*)inet_ntop(AF_INET6, entry.group_address.addr, buf,
                                   sizeof(buf));
          _last = entry.last_reporter_flag;
          _state = entry.group_state;
          _timer = entry.timer;
          _use = entry.use;
        }
        /*size_t jsonSize() {
          return JSON_ARRAY_SIZE(5) + JSON_STRING_SIZE(_addr.size());
        }*/
        void toJson(JsonArray a) {
          a = a.add<JsonArray>();
          a.add(_addr);
          a.add(_state);
          a.add(_last);
          a.add(_timer);
          a.add(_use);
        }

       private:
        std::string _addr;
        bool _last;
        u8_t _state;
        u16_t _timer;
        u8_t _use;
      };

      JsonDocument *Mld6::getState(RequestContext &ctx) {
        char ifname[4];
        std::map<struct netif*, std::vector<Group> > groups;
        struct netif* netif;
        //size_t size = 0;

        NETIF_FOREACH(netif) {
          struct mld_group* group = netif_mld6_data(netif);
          if (group) {
          //  size += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(3);
            auto& ng = groups[netif];
            while (group) {
              ng.emplace_back(*group);
            //  size += JSON_ARRAY_SIZE(1) + ng.back().jsonSize();
              group = group->next;
            }
          }
        }
        JsonDocument* doc = new JsonDocument(); /* size */
        auto root = doc->to<JsonObject>();
        for (auto& kv : groups)
          if (kv.second.size()) {
            esp32m::lwip::netif_name(kv.first, ifname, sizeof(ifname));
            auto a = root[ifname].to<JsonArray>();
            for (auto& g : kv.second) g.toJson(a);
          }

        return doc;
      }

      Mld6* useMld6() {
        return &Mld6::instance();
      }

    }  // namespace lwip
  }    // namespace debug
}  // namespace esp32m
#endif