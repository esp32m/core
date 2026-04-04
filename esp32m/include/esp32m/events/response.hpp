#pragma once

#include <ArduinoJson.h>
#include <esp_err.h>
#include <memory>

#include "esp32m/errors.hpp"
#include "esp32m/events.hpp"
#include "esp32m/events/request.hpp"

namespace esp32m {

  class Response : public Event {
   public:
    Response(const char* transport, const Request& request, const char* source)
        : Event(Type),
          _transport(transport),
          _name(request.name() ? request.name() : ""),
          _source(source ? source : ""),
          _seq(request.seq()),
          _createdAt(millis()) {
      auto data = request.data();
      if (!data.isNull()) {
        JsonDocument* doc = new JsonDocument();
        doc->set(data);
        _requestData.reset(doc);
      }
    }
    Response(const Response&) = delete;
    const char* name() const {
      return _name.c_str();
    }
    const char* transport() const {
      return _transport;
    }
    int seq() const {
      return _seq;
    }
    uint32_t createdAt() const {
      return _createdAt;
    }
    const char* source() const {
      return _source.c_str();
    }
    JsonDocument* data() {
      return _data.get();
    }
    JsonDocument* requestData() {
      return _requestData.get();
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
      if (err == ESP_OK) {
        _data.reset();
        _isError = false;
        return;
      }
      JsonDocument* doc = new JsonDocument();
      doc->set(err);
      setData(doc);
      _isError = true;
    }
    void setError(JsonDocument* data) {
      _data.reset(data);
      _isError = true;
    }
    void setData(JsonDocument* data) {
      _data.reset(data);
      _isError = false;
    }

    bool is(const char* name) const {
      return name && _name == name;
    }
    static bool is(Event& ev, const char* transport, Response** r) {
      if (!ev.is(Type) || strcmp(transport, ((Response&)ev).transport()))
        return false;
      if (r)
        *r = (Response*)&ev;
      return true;
    }
    static void respond(std::unique_ptr<Response>& response,
                        JsonDocument* data = nullptr) {
      response->setData(data);
      response->publish();
      response.reset();
    }
    /*static void respondError(std::unique_ptr<Response> &response,
                             ErrorList &errors) {
      auto doc = errors.toJson(nullptr);
      response->setError(doc);
      response->publish();
      response.reset();
    }*/

   protected:
    constexpr static const char* Type = "response";

   private:
    std::unique_ptr<JsonDocument> _data;
    std::unique_ptr<JsonDocument> _requestData;
    bool _isError = false, _partial = false;

    const char* _transport;
    // need to own these as it may be non-static
    std::string _name, _source;
    int _seq;
    uint32_t _createdAt;
    friend class Request;
  };

}  // namespace esp32m