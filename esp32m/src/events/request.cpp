#include "esp32m/events/request.hpp"
#include "esp32m/json.hpp"

namespace esp32m {

  bool Request::is(const char *name) const {
    return _name && name && !strcmp(_name, name);
  }

  bool Request::is(const char *target, const char *name) const {
    if (_target && target && strcmp(_target, target))
      return false;
    return _name && name && !strcmp(_name, name);
  }

  bool Request::is(Event &ev, const char *target, Request **r) {
    if (!ev.is(Type))
      return false;
    const char *t = ((Request &)ev)._target;
    if (t && strcmp(t, target))
      return false;
    if (r)
      *r = (Request *)&ev;
    return true;
  }

  bool Request::is(Event &ev, const char *target, const char *name,
                   Request **r) {
    if (!ev.is(Type))
      return false;
    const char *t = ((Request &)ev)._target;
    if (!t || strcmp(t, target))
      return false;
    const char *n = ((Request &)ev)._name;
    if (!n || strcmp(n, name))
      return false;
    if (r)
      *r = (Request *)&ev;
    return true;
  }

  void Request::respond(const char *source, const JsonVariantConst data,
                        bool error) {
    respondImpl(source, data, error);
    _handled = true;
  }

  void Request::respond(const char *source,
                        const JsonVariantConst dataOrError) {
    JsonObjectConst obj = dataOrError.as<JsonObjectConst>();
    if (obj && obj.size() == 1 && obj["error"])
      respondImpl(source, obj["error"], true);
    else
      respondImpl(source, dataOrError, false);
    _handled = true;
  }

  void Request::respond(const JsonVariantConst data, bool error) {
    respond(target(), data, error);
  }

  void Request::respond() {
    respond(target(), json::null<JsonVariantConst>(), false);
  }

  void Request::respond(const char *source, esp_err_t error) {
    if (error == ESP_OK)
      respond(source, json::null<JsonVariantConst>(), false);
    else {
      JsonDocument doc; // JSON_OBJECT_SIZE(1)
      doc.set(error);
      respondImpl(source, doc, true);
      _handled = true;
    }
  }

  void Request::respondError(esp_err_t error, const char *msg) {
    if (!msg)
      respond(error);
    else {
      JsonDocument doc; //JSON_ARRAY_SIZE(2)
      auto root = doc.to<JsonArray>();
      root.add(error);
      root.add(msg);
      respondImpl(target(), doc, true);
      _handled = true;
    }
  }

  void Request::respondError(const char *code) {
    JsonDocument doc; //JSON_ARRAY_SIZE(1)
    auto root = doc.to<JsonArray>();
    root.add(code);
    respondImpl(target(), doc, true);
    _handled = true;
  }

  void Request::respond(esp_err_t error) {
    if (error == ESP_OK)
      respond();
    else {
      JsonDocument doc; // JSON_OBJECT_SIZE(1)
      auto root = doc.to<JsonArray>();
      root.add(error);
      respondImpl(target(), doc, true);
      _handled = true;
    }
  }

  Response *Request::makeResponse() {
    _handled = true;
    return makeResponseImpl();
  }

  void Request::publish() {
    static JsonDocument errors; // <JSON_ARRAY_SIZE(1)>
    if (!errors.size())
      errors.add("unhandled");
    Event::publish();
    if (_handled)
      return;
    respond(errors[0], true);
  }

}  // namespace esp32m
