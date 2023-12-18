#pragma once

#include <ArduinoJson.h>
#include <esp_err.h>
#include <memory>

#include "esp32m/errors.hpp"
#include "esp32m/events.hpp"

namespace esp32m {

  class Response : public Event {
   public:
    Response(const char *transport, const char *name, const char *source,
             int seq)
        : Event(Type),
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
    void setError(esp_err_t err) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
      doc->set(err);
      setData(doc);
      _isError = true;
    }
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
    static bool is(Event &ev, const char *transport, Response **r) {
      if (!ev.is(Type) || strcmp(transport, ((Response &)ev).transport()))
        return false;
      if (r)
        *r = (Response *)&ev;
      return true;
    }
    static void respond(std::unique_ptr<Response> &response,
                        DynamicJsonDocument *data = nullptr) {
      response->setData(data);
      response->publish();
      response.reset();
    }
    static void respondError(std::unique_ptr<Response> &response,
                             ErrorList &errors) {
      auto doc = errors.toJson(nullptr);
      response->setError(doc);
      response->publish();
      response.reset();
    }

   protected:
    constexpr static const char *Type = "response";

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