#pragma once

#include <ArduinoJson.h>
#include <esp_err.h>
#include <memory>

#include "esp32m/events.hpp"

namespace esp32m {

  class Response : public Event {
   public:
    Response(const char *transport, const char *name, const char *source,
             int seq)
        : Event(NAME),
          _transport(transport),
          _name(name ? strdup(name) : nullptr),
          _source(source ? strdup(source) : nullptr),
          _seq(seq) {}
    Response(const Response &) = delete;
    virtual ~Response() {
      if (_name)
        delete _name;
      if (_source)
        delete _source;
      if (_data)
        delete _data;
    }
    const char *name() const {
      return _name;
    }
    const char *transport() const {
      return _transport;
    }
    int seq() const {
      return _seq;
    }
    const char *source() const {
      return _source;
    }
    DynamicJsonDocument *data() {
      return _data;
    }
    bool isPartial() const {
      return _partial;
    }
    void setPartial(bool partial = true) {
      _partial = partial;
    };
    bool isError() const {
      return _isError;
    }
    void setError(esp_err_t err);
    void setError(DynamicJsonDocument *data) {
      if (_data)
        delete (_data);
      _data = data;
      _isError = true;
    }
    void setData(DynamicJsonDocument *data) {
      if (_data)
        delete (_data);
      _data = data;
      _isError = false;
    }

    bool is(const char *name) const {
      return name && !strcmp(_name, name);
    }
    static bool is(Event &ev, const char *transport, Response **r);

   protected:
    static const char *NAME;

   private:
    DynamicJsonDocument *_data = nullptr;
    bool _isError = false, _partial = false;

    const char *_transport;
    char *_name,
        *_source;  // need to own this as it may come from non-static source
    int _seq;
    friend class Request;
  };

}  // namespace esp32m