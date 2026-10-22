#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>

static uint64_t clockMs = 0;
int64_t esp_timer_get_time() { return int64_t(clockMs * 1000); }
namespace esp32m::dev {
enum class StateClass { Undefined };
namespace ComponentType { constexpr const char* Sensor = "sensor"; }
struct Device {
  bool disabled = false;
  uint64_t generation = 0;
  uint64_t sampleGeneration() const { return generation; }
};
class Component {
 public:
  enum Flags { None = 0, Sensor = 1, HasState = 2 };
  virtual ~Component() = default;
  virtual const char* type() const = 0;
  virtual const char* component() const = 0;
  virtual const char* id() const = 0;
  Device* device() const { return _device; }
  bool isDisabled() const { return _device && _device->disabled; }
  JsonVariantConst getState() const { return _state.as<JsonVariantConst>(); }
  template <typename T> void setState(T value, bool* changed = nullptr) {
    const bool ch = _state.as<JsonVariantConst>() != value;
    if (changed) *changed = ch;
    _state.set(value);
  }
  void resetState(bool* changed = nullptr) { setState(JsonVariantConst{}, changed); }
 protected:
  explicit Component(const char* id) : _id(id ? id : "") {}
  void init(Device* d) { _device = d; }
  Flags _flags = None;
  std::string _id;
 private:
  Device* _device = nullptr;
  JsonDocument _state;
};
}
#include "sensor.inc"

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
int main() {
  using namespace esp32m::dev;
  Device device;
  Sensor sensor(&device, "temperature");
  CHECK(!sensor.sample().valid);
  sensor.set(19.5f);
  CHECK(sensor.sample().valid && sensor.sample().sampledAtMs == 0);
  clockMs = 1000;
  bool changed = true;
  sensor.set(19.5f, &changed);
  CHECK(!changed && sensor.sample().sampledAtMs == 1000);
  clockMs = 5000;
  sensor.setSample(20.f, 2000);
  CHECK(sensor.sample().sampledAtMs == 2000 && sensor.sample().value == 20);
  sensor.set(NAN);
  CHECK(!sensor.sample().valid);
  sensor.set(INFINITY);
  CHECK(!sensor.sample().valid);
  sensor.set(20.2);
  sensor.resetState();
  CHECK(!sensor.sample().valid);
  sensor.setState(22.1);
  CHECK(sensor.sample().valid);
  sensor.set("22.1");
  CHECK(!sensor.sample().valid);
  JsonDocument doc; doc.set(21.5);
  sensor.set(doc.as<JsonVariantConst>());
  CHECK(sensor.sample().valid && sensor.sample().value == 21.5);
  device.disabled = true;
  CHECK(!sensor.sample().valid);
  device.generation++;
  device.disabled = false;
  CHECK(!sensor.sample().valid);
  sensor.set(23);
  CHECK(sensor.sample().valid);
  sensor.invalidateSample();
  CHECK(!sensor.sample().valid);
  std::cout << "temperature samples: stable refresh, native time, invalidation and device generation passed\n";
}
