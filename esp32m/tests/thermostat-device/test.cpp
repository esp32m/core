#include <ArduinoJson.h>
#include "esp32m/dev/thermostat/controller.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_STATE = 0x103;
uint64_t fakeNowMs = 1000000;
int64_t esp_timer_get_time() { return fakeNowMs * 1000; }

namespace esp32m {
class App {
 public:
  static App& instance() { static App value; return value; }
  const char* hostname() const { return "test-host"; }
};
namespace net {
struct Message {
  std::string topic, payload;
  bool valid() const { return !topic.empty() && !payload.empty(); }
};
class Mqtt {
 public:
  static Mqtt& instance() { static Mqtt value; return value; }
  const Message& getBirth() const { return birth; }
  const Message& getLwt() const { return lwt; }
  Message birth{"esp32m/test-host/availability", "online"};
  Message lwt{"esp32m/test-host/availability", "offline"};
};
}
class Request {
 public:
  Request(const char* name = "state-set", bool internal = false, JsonVariantConst data = {},
          const char* target = nullptr)
      : _name(name), _internal(internal), _data(data), _target(target) {}
  virtual ~Request() = default;
  bool is(const char* name) const { return !strcmp(name, _name); }
  bool isInternal() const { return _internal; }
  JsonVariantConst data() const { return _data; }
  const char* target() const { return _target; }
  void respond(const char* source, JsonVariantConst data) {
    ++acknowledgements;
    responseSources.emplace_back(source);
    responses.emplace_back(); responses.back().set(data);
  }
  void respond(JsonVariantConst data, bool) { respond("", data); }
  void respond(esp_err_t result) { ++acknowledgements; status = result; }
  void respond() { ++acknowledgements; status = ESP_OK; }
  std::vector<JsonDocument> responses;
  std::vector<std::string> responseSources;
  unsigned acknowledgements = 0;
  int status = -1;
 private:
  const char* _name;
  bool _internal;
  JsonVariantConst _data;
  const char* _target;
};
struct ErrorList {
  bool failed = false;
  void check(esp_err_t e, const char*) { failed |= e != ESP_OK; }
};
class RequestContext {
 public:
  RequestContext(Request& request, JsonVariantConst data) : request(request), data(data) {}
  Request& request;
  JsonVariantConst data;
  ErrorList errors;
};
class Device {
 public:
  enum Flags { None = 0, HasSensors = 1 };
  Device() { objects.push_back(this); }
  virtual ~Device() { objects.erase(std::remove(objects.begin(), objects.end(), this), objects.end()); }
  virtual const char* name() const { return "source"; }
  bool isDisabled() const { return false; }
  void setSensorsPollInterval(int) {}
  // Mirror AppObject's same-name/broadcast subscriber routing, then exercise
  // the production Thermostat::handleRequest before generic framework dispatch.
  static void dispatch(Request& request) {
    for (auto* object : objects)
      if (!request.target() || !strcmp(request.target(), object->name())) object->handleRequest(request);
  }
  static JsonDocument describe() {
    JsonDocument result;
    auto root = result.to<JsonObject>();
    for (auto* object : objects) {
      const auto value = object->descriptor();
      if (!value.isNull()) root[object->name()] = value;
    }
    return result;
  }
 protected:
  void init(Flags) {}
  virtual const JsonVariantConst descriptor() const { return {}; }
  virtual JsonDocument* getState(RequestContext&) { return nullptr; }
  virtual void setState(RequestContext&) {}
  virtual JsonDocument* getDeviceConfig(RequestContext&) { return nullptr; }
  virtual bool setDeviceConfig(RequestContext&) { return false; }
  virtual bool handleRequest(Request& request) {
    RequestContext context(request, request.data());
    if (request.is("state-get")) {
      std::unique_ptr<JsonDocument> doc(getState(context));
      if (doc) request.respond(name(), doc->as<JsonVariantConst>());
    } else if (request.is("config-get")) {
      std::unique_ptr<JsonDocument> doc(getDeviceConfig(context));
      if (doc) request.respond(name(), doc->as<JsonVariantConst>());
    } else if (request.is("info-get")) {
      const auto value = descriptor();
      if (!value.isNull()) request.respond(name(), value);
    } else if (request.is("state-set")) {
      setState(context);
      std::unique_ptr<JsonDocument> doc(getState(context));
      if (doc) request.respond(name(), doc->as<JsonVariantConst>());
      else request.respond();
    } else if (request.is("config-set")) {
      setDeviceConfig(context);
      std::unique_ptr<JsonDocument> doc(getDeviceConfig(context));
      if (doc) request.respond(name(), doc->as<JsonVariantConst>());
      else request.respond();
    } else return false;
    return true;
  }
  virtual bool pollSensors() { return true; }
 private:
  inline static std::vector<Device*> objects;
};
namespace dev {
class Component;
std::vector<Component*> registry;
class Component {
 public:
  enum Flags { None = 0, RetainState = 1, HasState = 2, AcceptsCommands = 4 };
  explicit Component(const char* id) : _id(id ? id : "sensor") {}
  virtual ~Component() { registry.erase(std::remove(registry.begin(), registry.end(), this), registry.end()); }
  virtual const char* component() const = 0;
  virtual const char* type() const = 0;
  virtual bool isSensor() const { return false; }
  void setTitle(const char* title) { _title = title; }
  const char* title() const { return _title; }
  const char* id() const { return _id.c_str(); }
  std::string uid() const { return std::string(_device->name()) + "_" + id(); }
  Device* device() const { return _device; }
  bool isDisabled() const { return _disabled; }
  void setDisabled(bool disabled) { _disabled = disabled; }
  bool retain() const { return (_flags & RetainState) != 0; }
  template<class T> void setState(T value) { _state.set(value); }
  JsonDocument _state;
 protected:
  Flags _flags = None;
  void init(Device* device) { _device = device; registry.push_back(this); }
 private:
  std::string _id;
  const char* _title = nullptr;
  Device* _device = nullptr;
  bool _disabled = false;
};
class Sensor : public Component {
 public:
  struct Sample { double value = NAN; uint64_t sampledAtMs = 0; bool valid = false; };
  Sensor(Device* device, const char* type, const char* id = nullptr)
      : Component(id ? id : type), _type(type) { init(device); }
  const char* component() const override { return "sensor"; }
  const char* type() const override { return _type; }
  bool isSensor() const override { return true; }
  template<class T> void set(T value) { Component::setState(value); }
  void sampleValue(double value, uint64_t stamp) { _sample = {value, stamp, true}; }
  Sample sample() const { return _sample; }
  const char* unit = nullptr;
 private:
  const char* _type;
  Sample _sample;
};
class BinarySensor : public Sensor {
 public:
  BinarySensor(Device* device, const char* type, const char* id = nullptr) : Sensor(device, type, id) { _flags = RetainState; }
};
class AllComponents {
 public:
  auto begin() const { return registry.begin(); }
  auto end() const { return registry.end(); }
};
}  // namespace dev
namespace config { int publishes = 0; struct Changed { static void publish(Device*) { ++publishes; } }; }
namespace integrations::ha {
struct DescribeRequest {
  static constexpr const char* Name = "ha-describe";
  static void autoRespond(Request&, dev::Sensor&) {}
};
class CommandRequest : public Request {
 public:
  static constexpr const char* Name = "ha-command";
  CommandRequest(const char* component, JsonVariantConst data, bool retained = false)
      : Request(Name, false, data), componentId(component), retained(retained) {}
  std::string componentId;
  bool retained;
};
}  // namespace integrations::ha
}  // namespace esp32m

#include "production.inc"

namespace {
using namespace esp32m;
using namespace esp32m::dev;
namespace th = esp32m::dev::thermostat;
class Actuator : public th::Actuator {
 public:
  Actuator() {
    cap.startStop = cap.nativeTarget = cap.nativeTemperature = true;
    feedback.valid = true;
    feedback.sampledAtMs = fakeNowMs;
    feedback.phase = th::Phase::Off;
    feedback.nativeTemperature = {15, fakeNowMs, true};
  }
  th::Capabilities cap;
  th::Feedback feedback;
  std::vector<th::Request> calls;
  std::vector<bool> attachments;
  th::Capabilities capabilities() const override { return cap; }
  th::Feedback snapshot() const override { return feedback; }
  th::Result apply(const th::Request& request) override { calls.push_back(request); return th::Result::Accepted; }
 protected:
  void thermostatAttached(bool attached) override { attachments.push_back(attached); }
};
class ThermostatTest : public Thermostat {
 public:
  using Thermostat::Thermostat;
  using Thermostat::pollSensors;
  using Thermostat::handleRequest;
  bool configure(const char* json, bool internal = false) {
    JsonDocument doc; assert(!deserializeJson(doc, json));
    Request request("config-set", internal);
    RequestContext context(request, doc.as<JsonVariantConst>());
    setDeviceConfig(context);
    return !context.errors.failed;
  }
  JsonDocument state() {
    Request request; RequestContext context(request, {});
    std::unique_ptr<JsonDocument> doc(getState(context)); return doc ? std::move(*doc) : JsonDocument{};
  }
  JsonDocument configuration() {
    Request request; RequestContext context(request, {});
    std::unique_ptr<JsonDocument> doc(getDeviceConfig(context)); return doc ? std::move(*doc) : JsonDocument{};
  }
};

void shadowAndBoot() {
  Actuator actuator;
  ThermostatTest thermostat(actuator, "room");
  thermostat.pollSensors();
  assert(actuator.calls.empty());
  assert(!thermostat.state()["config"]["heat"].as<bool>());
  assert(thermostat.configure(R"({"enabled":true,"heat":true,"shadow":true})"));
  thermostat.pollSensors();
  assert(actuator.calls.empty());
  assert(thermostat.state()["climate"]["mode"] == "heat");
  assert(!thermostat.state()["climate"]["available"].as<bool>());
  assert(!thermostat.configuration()["heat"].as<bool>());
  assert(thermostat.configure(R"({"enabled":true,"heat":true,"shadow":false})", true));
  thermostat.pollSensors();
  assert(actuator.calls.empty());
  assert(thermostat.state()["climate"]["mode"] == "off");
}
void configIsAtomic() {
  Actuator actuator; ThermostatTest thermostat(actuator);
  assert(!thermostat.configure(R"({"targetC":22,"minimumOffMs":-1})"));
  assert(thermostat.state()["config"]["targetC"].as<float>() == 18);
  assert(!thermostat.configure(R"({"enabled":"true"})"));
  assert(!thermostat.configure(R"({"unknown":1})"));
  assert(!thermostat.configure(R"({"targetC":36})")); // Exceeds configured high-temperature limit.
}
void sourcesAndFreshness() {
  Device source;
  Sensor c(&source, "temperature", "c"), f(&source, "temperature", "f"),
      k(&source, "temperature", "k"), bad(&source, "temperature", "bad"),
      humidity(&source, "humidity", "h");
  c.setTitle("Room probe"); c.sampleValue(19, fakeNowMs);
  f.unit = "°F"; f.sampleValue(68, fakeNowMs);
  k.unit = "K"; k.sampleValue(294.15, fakeNowMs);
  bad.unit = "raw"; bad.sampleValue(22, fakeNowMs);
  humidity.sampleValue(50, fakeNowMs);
  Actuator actuator; ThermostatTest thermostat(actuator);
  assert(thermostat.configure(R"({"sensorUid":"source_f"})"));
  thermostat.pollSensors();
  auto state = thermostat.state();
  assert(state["sources"].size() == 4);
  const auto sampledAt = fakeNowMs;
  assert(state["source"]["sampledAtMs"] == sampledAt && state["source"]["ageMs"].isUnbound());
  assert(std::fabs(state["climate"]["currentTemperature"].as<float>() - 20) < .01);
  for (auto item : state["sources"].as<JsonArray>()) {
    assert(item["sampledAtMs"] == sampledAt && item["ageMs"].isUnbound());
    if (item["uid"] == "source_k") assert(std::fabs(item["valueC"].as<float>() - 21) < .01);
    if (item["uid"] == "source_bad") assert(!item["valid"].as<bool>());
  }
  fakeNowMs += 31000;
  state = thermostat.state();
  assert(state["climate"]["currentTemperature"].isNull());
  assert(!state["source"]["valid"].as<bool>());
  assert(state["source"]["sampledAtMs"] == sampledAt);
  f.setDisabled(true); f.sampleValue(68, fakeNowMs); thermostat.pollSensors();
  assert(!thermostat.state()["source"]["valid"].as<bool>());
  fakeNowMs -= 31000;
}
void haCommandsAndDiscovery() {
  Actuator actuator; ThermostatTest thermostat(actuator, "room");
  JsonDocument payload; payload.set(R"({"mode":"heat"})");
  integrations::ha::CommandRequest retained("climate", payload.as<JsonVariantConst>(), true);
  assert(thermostat.handleRequest(retained));
  assert(retained.status == ESP_ERR_INVALID_STATE);
  assert(thermostat.state()["climate"]["mode"] == "off");
  integrations::ha::CommandRequest live("climate", payload.as<JsonVariantConst>());
  assert(thermostat.handleRequest(live) && live.status == ESP_OK);
  assert(thermostat.state()["climate"]["mode"] == "heat");
  payload.set(R"({"mode":"auto"})");
  integrations::ha::CommandRequest invalid("climate", payload.as<JsonVariantConst>());
  assert(thermostat.handleRequest(invalid) && invalid.status == ESP_ERR_INVALID_ARG);
  assert(thermostat.state()["climate"]["mode"] == "heat");
  Request describe("ha-describe"); assert(thermostat.handleRequest(describe));
  assert(describe.responses.size() == 1);
  auto config = describe.responses[0]["config"];
  assert(config["modes"].size() == 2 && config["modes"][0] == "off" && config["modes"][1] == "heat");
  assert(!config["optimistic"].as<bool>());
  assert(config["availability_mode"] == "all" && config["availability"].size() == 2);
  assert(config["availability"][0]["topic"] == "esp32m/test-host/availability");
  assert(config["availability"][1]["topic"] == "esp32m/test-host/room/state");
  for (auto* component : registry) if (component->device() == &thermostat) assert(!component->retain());
}
void manualInhibitPersists() {
  Actuator actuator; ThermostatTest thermostat(actuator);
  assert(thermostat.configure(R"({"enabled":true,"heat":true,"shadow":false,"minimumOffMs":0})"));
  thermostat.pollSensors();
  actuator.feedback.revision++;
  thermostat.pollSensors();
  assert(thermostat.state()["control"]["inhibited"].as<bool>());
  assert(thermostat.configure(R"({"targetC":20})"));
  thermostat.pollSensors();
  assert(thermostat.state()["control"]["inhibited"].as<bool>());
  JsonDocument payload; payload.set(R"({"resume":true})");
  integrations::ha::CommandRequest resume("climate", payload.as<JsonVariantConst>());
  thermostat.handleRequest(resume);
  thermostat.pollSensors();
  assert(!thermostat.state()["control"]["inhibited"].as<bool>());
}
void openthermIntentRestore() {
  Actuator actuator; actuator.cap.deviceGate = true; actuator.cap.flow = true;
  actuator.cap.nativeTarget = false;
  ThermostatTest thermostat(actuator);
  assert(thermostat.configure(R"({"heat":true,"enabled":true,"shadow":true})", true));
  assert(thermostat.configuration()["heat"].as<bool>());
  thermostat.pollSensors(); assert(actuator.calls.empty());
}
void coolingAndOwnedRelease() {
  Actuator actuator;
  {
    ThermostatTest thermostat(actuator);
    assert(thermostat.configure(R"({"enabled":true,"heat":true,"shadow":false,"minimumOffMs":0})"));
    thermostat.pollSensors();
    assert(!actuator.calls.empty());
    actuator.feedback.phase = th::Phase::Cooling;
    thermostat.pollSensors();
    assert(thermostat.state()["climate"]["action"] == "fan");
    fakeNowMs += 6000;
    assert(thermostat.state()["climate"]["action"].isNull());
    fakeNowMs -= 6000;
  }
  assert(actuator.calls.back().command == th::Command::Release);
  assert((actuator.attachments == std::vector<bool>{true, false}));
}
void duplicateAttachmentIsIsolated() {
  Actuator actuator;
  {
    ThermostatTest first(actuator, "same-room");
    assert(first.configure(R"({"enabled":true,"heat":true,"shadow":false,"minimumOffMs":0})"));
    first.pollSensors();
    assert(first.state()["control"]["attached"].as<bool>());
    const auto componentCount = registry.size();
    const auto calls = actuator.calls.size();
    {
      ThermostatTest duplicate(actuator, "same-room");
      assert(registry.size() == componentCount);
      assert(duplicate.configure(R"({"enabled":true,"heat":true,"shadow":false,"minimumOffMs":0})"));
      duplicate.pollSensors();
      assert(duplicate.state().isNull());
      assert(duplicate.configuration().isNull());
      Request describe("ha-describe"); duplicate.handleRequest(describe);
      assert(describe.responses.empty());
      assert(actuator.calls.size() == calls);
    }
    assert(actuator.calls.size() == calls);
    assert((actuator.attachments == std::vector<bool>{true}));
    assert(registry.size() == componentCount);
    first.pollSensors();
    assert(first.state()["control"]["attached"].as<bool>());
  }
  assert((actuator.attachments == std::vector<bool>{true, false}));
}
void manualRevisionPreventsDestructorRelease() {
  Actuator actuator;
  size_t applied = 0;
  {
    ThermostatTest thermostat(actuator);
    assert(thermostat.configure(R"({"enabled":true,"heat":true,"shadow":false,"minimumOffMs":0})"));
    thermostat.pollSensors();
    applied = actuator.calls.size();
    assert(applied);
    ++actuator.feedback.revision;
  }
  assert(actuator.calls.size() == applied);
  assert((actuator.attachments == std::vector<bool>{true, false}));
}
void ownedAndBorrowedLifetime() {
  std::vector<std::string> events;
  class LifetimeActuator final : public Actuator {
   public:
    explicit LifetimeActuator(std::vector<std::string>& events) : events(events) {}
    ~LifetimeActuator() override { events.push_back("destroy"); }
    th::Result apply(const th::Request& request) override {
      if (request.command == th::Command::Release) events.push_back("release");
      return Actuator::apply(request);
    }
   protected:
    void thermostatAttached(bool attached) override {
      events.push_back(attached ? "attach" : "detach");
      Actuator::thermostatAttached(attached);
    }
   private:
    std::vector<std::string>& events;
  };
  {
    auto actuator = std::make_unique<LifetimeActuator>(events);
    ThermostatTest thermostat(std::move(actuator), "owned");
    assert(thermostat.configure(R"({"enabled":true,"heat":true,"shadow":false,"minimumOffMs":0})"));
    thermostat.pollSensors();
  }
  assert((events == std::vector<std::string>{"attach", "release", "detach", "destroy"}));
  events.clear();
  {
    LifetimeActuator actuator(events);
    {
      ThermostatTest thermostat(actuator, "borrowed");
      assert((events == std::vector<std::string>{"attach"}));
    }
    assert((events == std::vector<std::string>{"attach", "detach"}));
    const int newOwner = 0;
    assert(actuator.attachThermostat(&newOwner));
    actuator.detachThermostat(&newOwner);
  }
  assert((events == std::vector<std::string>{"attach", "detach", "attach", "detach", "destroy"}));
}
void directDeviceHelperNaming() {
  class Driver : public Device, public Actuator {
   public:
    const char* name() const override { return "heater"; }
  } driver;
  {
    std::unique_ptr<Thermostat> thermostat(useThermostat(driver));
    assert(!strcmp(thermostat->name(), "heater_thermostat"));
    assert(driver.calls.empty());
  }
  {
    std::unique_ptr<Thermostat> thermostat(useThermostat(driver, "my-room"));
    assert(!strcmp(thermostat->name(), "my-room"));
  }
  {
    std::unique_ptr<Thermostat> thermostat(useThermostat(driver, ""));
    assert(!strcmp(thermostat->name(), "heater_thermostat"));
  }
  assert((driver.attachments == std::vector<bool>{true, false, true, false, true, false}));
}
void duplicateRequestDispatchCannotClobberOwner() {
  Actuator actuator;
  ThermostatTest first(actuator, "same-room");
  assert(first.configure(R"({"enabled":true,"targetC":21,"shadow":true})"));
  first.pollSensors();
  {
    ThermostatTest duplicate(actuator, "same-room");
    for (const char* operation : {"state-get", "config-get", "info-get", "available-sensors"}) {
      Request request(operation, false, {}, "same-room");
      Device::dispatch(request);
      assert(request.acknowledgements == 1 && request.responses.size() == 1);
      if (!strcmp(operation, "state-get"))
        assert(request.responses.back()["config"]["targetC"].as<float>() == 21);
      if (!strcmp(operation, "config-get"))
        assert(request.responses.back()["targetC"].as<float>() == 21);
    }
    JsonDocument update;
    update["targetC"] = 22;
    Request configure("config-set", false, update.as<JsonVariantConst>(), "same-room");
    Device::dispatch(configure);
    assert(configure.acknowledgements == 1 && configure.responses.size() == 1);
    assert(configure.responses.back()["targetC"].as<float>() == 22);
    update.clear(); update["mode"] = "heat";
    Request mode("state-set", false, update.as<JsonVariantConst>(), "same-room");
    Device::dispatch(mode);
    assert(mode.acknowledgements == 1 && mode.responses.size() == 1);
    assert(mode.responses.back()["config"]["targetC"].as<float>() == 22);
    assert(mode.responses.back()["climate"]["mode"] == "heat");

    JsonDocument payload; payload.set(R"({"temperature":23})");
    integrations::ha::CommandRequest command("climate", payload.as<JsonVariantConst>());
    Device::dispatch(command);
    assert(command.acknowledgements == 1 && command.status == ESP_OK);
    assert(first.state()["config"]["targetC"].as<float>() == 23);
    assert(duplicate.state().isNull() && duplicate.configuration().isNull());

    auto description = Device::describe();
    assert(description.size() == 1 && description["same-room"][0] == "thermostat");
  }
  Request after("state-get", false, {}, "same-room");
  Device::dispatch(after);
  assert(after.responses.size() == 1 && after.responses.back()["config"]["targetC"].as<float>() == 23);
  assert((actuator.attachments == std::vector<bool>{true}));
}
void nullActuatorRetainsDiagnostics() {
  ThermostatTest thermostat(std::unique_ptr<th::Actuator>{}, "missing-actuator");
  thermostat.pollSensors();
  Request request("state-get", false, {}, "missing-actuator");
  Device::dispatch(request);
  assert(request.responses.size() == 1);
  assert(request.responses[0]["control"]["reason"] == "no-actuator");
  assert(!request.responses[0]["control"]["attached"].as<bool>());
  assert(Device::describe()["missing-actuator"][0] == "thermostat");
}
}
int main() {
  shadowAndBoot(); configIsAtomic(); sourcesAndFreshness();
  haCommandsAndDiscovery(); manualInhibitPersists(); openthermIntentRestore();
  coolingAndOwnedRelease();
  duplicateAttachmentIsIsolated(); manualRevisionPreventsDestructorRelease();
  ownedAndBorrowedLifetime(); directDeviceHelperNaming();
  duplicateRequestDispatchCannotClobberOwner(); nullActuatorRetainsDiagnostics();
  assert(registry.empty());
  std::cout << "thermostat device: 13 groups passed\n";
}
