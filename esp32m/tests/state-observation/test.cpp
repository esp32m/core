#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

constexpr int ESP_ERR_NO_MEM = 257;

namespace esp32m {
  class AppObject;
  namespace log {
    struct Loggable {
      virtual ~Loggable() = default;
      virtual const char* name() const { return "object"; }
    };
  }
  namespace json {
    JsonVariantConst emptyArray() {
      static JsonDocument doc;
      if (doc.isNull()) doc.to<JsonArray>();
      return doc.as<JsonVariantConst>();
    }
    bool check(const void*, JsonDocument* doc, const char*) { return !doc->overflowed(); }
  }
  class Event {
   public:
    explicit Event(const char* type) : _type(type) {}
    virtual ~Event() = default;
    bool is(const char* type) const { return !std::strcmp(type, _type); }
    void publish();
   private:
    const char* _type;
  };
  struct Subscription {
    std::function<void(Event&)> callback;
  };
  struct EventManager {
    std::vector<Subscription*> subscriptions;
    static EventManager& instance() { static EventManager value; return value; }
    Subscription* subscribe(std::function<void(Event&)> callback) {
      auto result = new Subscription{callback};
      subscriptions.push_back(result);
      return result;
    }
    void publish(Event& event) {
      auto subscribers = subscriptions;
      for (auto* subscription : subscribers) subscription->callback(event);
    }
  };
  void Event::publish() { EventManager::instance().publish(*this); }
  struct Config {
    static constexpr const char* KeyConfigGet = "config-get";
    static constexpr const char* KeyConfigSet = "config-set";
  };
  namespace config {
    struct Changed { static void publish(AppObject*) {} };
  }
  struct Broadcast { static void publish(const char*, const char*) {} };
  class Request : public Event {
   public:
    JsonDocument payload, response;
    std::string command;
    bool responded = false, error = false;
    explicit Request(const char* name) : Event("request"), command(name) {}
    const char* name() const { return command.c_str(); }
    bool is(const char* name) const { return command == name; }
    bool isBroadcast() const { return false; }
    const char* origin() const { return "test"; }
    JsonVariantConst data() const { return payload.as<JsonVariantConst>(); }
    void respond(const char*, JsonVariantConst state, bool failed) {
      response.set(state); responded = true; error = failed;
    }
    void respond(const char*, JsonDocument& state, bool error) {
      respond("test", state.as<JsonVariantConst>(), error);
    }
    void respond(const char*, int code) {
      response.set(code); responded = true; error = code != 0;
    }
    static bool is(Event& event, const char*, Request** request) {
      if (!event.is("request")) return false;
      *request = static_cast<Request*>(&event); return true;
    }
  };
  struct RequestContext {
    Request& request;
    JsonVariantConst data;
    struct Errors {
      bool failed = false;
      bool empty() const { return !failed; }
    } errors;
    std::unique_ptr<JsonDocument> result;
    RequestContext(Request& request, JsonVariantConst data) : request(request), data(data) {}
    void respond(const char* name) {
      request.respond(name, result ? result->as<JsonVariantConst>() : JsonVariantConst{}, errors.failed);
    }
  };
  struct EventDescribe {
    static bool is(Event&, EventDescribe**) { return false; }
    void add(const char*, JsonVariantConst) {}
  };
}

#include "production.inc"

namespace {
  void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
  }
  class Object : public esp32m::AppObject {
   public:
    unsigned state = 0, captured = 0;
    bool handling = false, overflowState = false;
   protected:
    struct RejectAllocator : ArduinoJson::Allocator {
      void* allocate(size_t) override { return nullptr; }
      void deallocate(void*) override {}
      void* reallocate(void*, size_t) override { return nullptr; }
    } allocator;
    bool handleRequest(esp32m::Request& request) override {
      handling = true;
      bool result = AppObject::handleRequest(request);
      if (!result && (request.is("run") || request.is("failed-command"))) {
        ++state;
        result = true;
      }
      handling = false;
      return result;
    }
    void setState(esp32m::RequestContext& context) override {
      ++state;
      if (context.data["fail"] == true) context.errors.failed = true;
    }
    JsonDocument* getState(esp32m::RequestContext&) override {
      ++captured;
      auto doc = overflowState ? new JsonDocument(&allocator) : new JsonDocument();
      (*doc)["value"] = state;
      return doc;
    }
    bool setConfig(esp32m::RequestContext& context) override {
      ++state;
      if (context.data["fail"] == true) context.errors.failed = true;
      return !context.errors.failed;
    }
  };
}

int main() {
  try {
    Object object;
    unsigned invalidations = 0;
    auto* listener = esp32m::EventManager::instance().subscribe([&](esp32m::Event& event) {
      esp32m::EventStateChanged* changed;
      if (!esp32m::EventStateChanged::is(event, &changed)) return;
      require(changed->object() == &object, "invalidation names owner");
      require(!object.handling, "invalidation waits until complete custom handler returns");
      require(changed->state().isUnbound(), "hint must not borrow a serialized snapshot");
      ++invalidations;
    });
    for (const auto* command : {"state-get", "info-get", "config-get", "device-state", "unhandled"}) {
      esp32m::Request request(command);
      request.publish();
      require(invalidations == 0, "reads and unsupported requests must not self-invalidate");
    }
    for (const auto* command : {"state-set", "config-set", "run", "failed-command"}) {
      esp32m::Request request(command);
      const auto previous = object.captured;
      request.publish();
      require(invalidations > 0, "handled command invalidates");
      if (request.is("run") || request.is("failed-command"))
        require(object.captured == previous, "invalidation never captures state inline");
    }
    require(invalidations == 4, "exactly one invalidation per handled command");
    for (const auto* command : {"state-set", "config-set"}) {
      esp32m::Request request(command);
      request.payload["fail"] = true;
      const auto previous = object.captured;
      request.publish();
      require(object.captured == previous, "failed mutation does not trigger inline snapshot");
    }
    require(invalidations == 6, "failed state/config mutations invalidate diagnostics");
    object.overflowState = true;
    esp32m::Request overflowing("state-get");
    overflowing.publish();
    require(overflowing.error && overflowing.response == ESP_ERR_NO_MEM,
            "source allocation failure must reject a truncated snapshot");
    require(invalidations == 6, "failed read cannot start an invalidation feedback loop");
    // AppObject owns its subscription; this listener has test lifetime only.
    delete listener;
    std::cout << "PASS: production AppObject state observation (reads, mutations, custom commands, errors, deferred capture)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
