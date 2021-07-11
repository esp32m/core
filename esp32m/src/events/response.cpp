#include "esp32m/events/response.hpp"
#include "esp32m/json.hpp"

namespace esp32m {

  const char *Response::NAME = "response";

  bool Response::is(Event &ev, const char *transport, Response **r) {
    if (!ev.is(NAME) || strcmp(transport, ((Response &)ev).transport()))
      return false;
    if (r)
      *r = (Response *)&ev;
    return true;
  }

  void Response::setError(esp_err_t err) {
    DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
    doc->set(err);
    setData(doc);
    _isError = true;
  }

}  // namespace esp32m
