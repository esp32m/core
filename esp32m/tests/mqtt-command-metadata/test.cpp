#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using esp_err_t = int;
enum esp_mqtt_event_id_t { MQTT_EVENT_ERROR, MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_SUBSCRIBED, MQTT_EVENT_UNSUBSCRIBED, MQTT_EVENT_DATA };
struct Error { int error_type = 0; };
struct esp_mqtt_event_t {
  const char* topic = nullptr;
  int topic_len = 0;
  const char* data = nullptr;
  int data_len = 0, total_data_len = 0, current_data_offset = 0;
  bool retain = false;
  Error* error_handle = nullptr;
};
using esp_mqtt_event_handle_t = esp_mqtt_event_t*;
template<class... T> void fakeLog(T...) {}
#define logE(...) fakeLog(__VA_ARGS__)
namespace esp32m::net {
class Mqtt;
namespace mqtt {
enum class Status { Connected, Disconnected };
class Incoming {
 public:
  Incoming(std::string, std::string) {}
  void publish() {}
};
#include "subscription.inc"
int subscriptionIdCounter = 0;
}
using namespace mqtt;
class Mqtt {
 public:
  static Mqtt& instance() { static Mqtt value; return value; }
  Subscription* subscribe(const char* topic, int qos = 0);
  Subscription* subscribe(const char* topic, HandlerFunction handler, int qos = 0);
  Subscription* subscribeDetailed(const char* topic, DetailedHandlerFunction handler, int qos = 0);
  std::vector<DetailedHandlerFunction> findMatchingHandlers(std::string topic);
  bool isConnected() const { return true; }
  bool intSubscribe(std::string, int) { return true; }
  void setStatus(Status) {}
  esp_err_t handle(int32_t id, void* event);
 private:
  std::mutex _mutex;
  std::map<std::string, std::map<int, Subscription*>> _subscriptions;
  uint32_t _timer = 0, _recvcnt = 0;
};
namespace mqtt { Subscription::~Subscription() {} }
#include "methods.inc"
}
namespace esp32m::integrations::ha {
struct CommandRecord { std::string component, payload; bool retained; };
std::vector<CommandRecord> commands;
}
namespace esp32m {
class Request {
 public:
  Request(const char*, int, const char*, JsonVariantConst data, const char*) : _data(data) {}
  virtual ~Request() = default;
  virtual void respondImpl(const char*, JsonVariantConst, bool) = 0;
  void publish();
 protected:
  JsonVariantConst _data;
};
namespace integrations::ha {
#include "command.inc"
namespace mqtt {
void wakeUp() {}
#include "dev.inc"
}
}
void Request::publish() {
  const auto& command = dynamic_cast<const integrations::ha::CommandRequest&>(*this);
  integrations::ha::commands.push_back({command.componentId, _data.as<std::string>(), command.retained});
}
}
int main() {
  using esp32m::net::Mqtt;
  using esp32m::integrations::ha::commands;
  auto& mqtt = Mqtt::instance();
  unsigned oldCalls = 0;
  std::unique_ptr<esp32m::net::Subscription> legacy(mqtt.subscribe("heater/command",
      [&](std::string topic, std::string payload) {
        assert(topic == "heater/command" && payload == "heat"); ++oldCalls;
      }));
  esp32m::integrations::ha::mqtt::Dev device("room", "room/state", "heater/command", "climate");
  esp_mqtt_event_t event;
  event.topic = "heater/command"; event.topic_len = strlen(event.topic);
  event.data = "heat"; event.data_len = event.total_data_len = 4;
  event.retain = true;
  mqtt.handle(MQTT_EVENT_DATA, &event);
  assert(oldCalls == 1 && commands.size() == 1 && commands.back().retained);
  assert(commands.back().component == "climate" && commands.back().payload == "heat");
  event.retain = false;
  mqtt.handle(MQTT_EVENT_DATA, &event);
  assert(oldCalls == 2 && commands.size() == 2 && !commands.back().retained);
  event.total_data_len = 12; // A prefix must never become a full HA control.
  mqtt.handle(MQTT_EVENT_DATA, &event);
  assert(oldCalls == 3 && commands.size() == 2);
  event.total_data_len = 4; event.current_data_offset = 4;
  mqtt.handle(MQTT_EVENT_DATA, &event);
  assert(oldCalls == 4 && commands.size() == 2);
  std::cout << "MQTT command metadata: retained, live, fragmentation and legacy compatibility passed\n";
}
