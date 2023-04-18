#pragma once

#include <esp_err.h>

#include <ArduinoJson.h>

#include "esp32m/base.hpp"
#include "esp32m/events.hpp"

namespace esp32m {
  class Response;

  class Request : public Event {
   public:
    const char *name() const {
      return _name;
    }
    int seq() const {
      return _seq;
    }
    const char *target() const {
      return _target;
    }
    const JsonVariantConst data() const {
      return _data;
    }
    bool isBroadcast() const {
      return !_target;
    }
    bool isResponded() const {
      return _handled;
    }
    const char *origin() const {
      return _origin;
    }
    /** returns true if the request originated from within esp32m */
    bool isInternal() const {
      return _origin == nullptr;
    }

    void publish() override;

    void respond(const char *source, const JsonVariantConst data, bool error);
    void respond(const char *source, const JsonVariantConst dataOrError);
    void respond(const char *source, esp_err_t error);
    void respond(const JsonVariantConst data, bool error);
    void respond(esp_err_t error);
    void respond();
    Response *makeResponse();

    bool is(const char *name) const;
    bool is(const char *target, const char *name) const;

    static bool is(Event &ev, const char *target, Request **r);
    static bool is(Event &ev, const char *target, const char *name,
                   Request **r);

   protected:
    Request(const char *name, int seq, const char *target,
            const JsonVariantConst data, const char *origin)
        : Event(NAME),
          _name(name),
          _seq(seq),
          _target(target),
          _data(data),
          _origin(origin) {}
    virtual void respondImpl(const char *source, const JsonVariantConst data,
                             bool error) = 0;
    virtual Response *makeResponseImpl() {
      assert(false);
    }
    static const char *NAME;

   private:
    const char *_name;
    int _seq;
    const char *_target;
    const JsonVariantConst _data;
    const char *_origin;
    bool _handled = false;
  };
  class RequestContext {
   public:
    RequestContext(const Request &request, const JsonVariantConst data)
        : request(request), data(data) {}
    const Request &request;
    const JsonVariantConst data;
    ErrorList errl;
    DynamicJsonDocument *result = nullptr;
  };

}  // namespace esp32m