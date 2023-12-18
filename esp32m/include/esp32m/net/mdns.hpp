#pragma once

#include "esp32m/app.hpp"

namespace esp32m {

  namespace net {

    class Mdns;

    namespace mdns {

      class EventPopulate : public Event {
       public:
        static bool is(Event &ev) {
          if (!ev.is(Type))
            return false;
          return true;
        }

       private:
        EventPopulate() : Event(Type) {}
        constexpr static const char *Type = "mdns-populate";
        friend class net::Mdns;
      };

      class Service {
       public:
        Service(const Service &) = delete;
        Service(const char *type, const char *proto, uint16_t port)
            : _type(type), _proto(proto), _port(port), _key(type) {
          _key += '.';
          _key += _proto;
        };
        void set(const char *key, const char *value) {
          _props[key] = value;
        }

       private:
        const char *_type;
        const char *_proto;
        uint16_t _port;
        std::string _key;
        std::map<std::string, std::string> _props;
        void sync();
        friend class net::Mdns;
      };
    }  // namespace mdns

    class Mdns : public AppObject {
     public:
      Mdns(const Mdns &) = delete;
      static Mdns &instance();
      const char *name() const override {
        return "mdns";
      }
      void set(mdns::Service *service) {
        if (!service)
          return;
        _services[service->_key] = std::unique_ptr<mdns::Service>(service);
        if (_initialized)
          service->sync();
      }

     protected:
      void handleEvent(Event &ev) override;

     private:
      bool _initialized = false;
      std::map<std::string, std::unique_ptr<mdns::Service> > _services;
      Mdns() {}
      void updateHostname();
      void updateServices();
    };

    Mdns *useMdns();
  }  // namespace net
}  // namespace esp32m