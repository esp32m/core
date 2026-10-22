#include <ArduinoJson.h>
#include "esp32m/debug/rs485/stats.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

constexpr int ESP_OK = 0;
namespace fake { uint64_t nowMs = 0; }
int64_t esp_timer_get_time() { return int64_t(fake::nowMs * 1000); }
#define ESP_ERROR_CHECK_WITHOUT_ABORT(value) (value)

namespace esp32m {
  struct RequestContext {};
  namespace json {
    template <class T> void to(JsonObject target, const char* key, T value) { target[key] = value; }
  }
  namespace dev {
    struct Counter {
      int count = 120, error = ESP_OK;
      int read(int& value, bool) { value = count; return error; }
    };
    struct Tach { Counter counter; Counter* pcnt() { return &counter; } };
    struct Sensor { float value = NAN; void set(float next) { value = next; } };
    struct Fan {
      Tach tach; Tach* _tach = &tach;
      void* _pwm = nullptr;
      Sensor sensor; Sensor* _sensorRpm = &sensor;
      uint64_t _stamp = 0;
      float _rpm = NAN, _speed = 0;
      bool _on = false;
      unsigned _ppr = 1;
      JsonDocument* getState(RequestContext&);
      bool pollSensors();
    };
    struct FlomarvelValve {
      static constexpr uint64_t StateFreshnessMs = 5000;
      uint8_t _addr = 3;
      uint64_t _actualStamp = 0, _targetStamp = 0, _probeStamp = 0;
      uint16_t _actualRaw = 100, _targetRaw = 200, _probeValidMask = 0;
      std::array<uint16_t, 8> _probeRegs{};
      struct Component {
        JsonDocument doc;
        Component() { doc["state"] = "open"; }
        JsonVariantConst getState() const { return doc.as<JsonVariantConst>(); }
      } _component;
      uint64_t nowMs() const { return fake::nowMs; }
      float actualAngle() const { return _actualRaw / 10.f; }
      float targetAngle() const { return _targetRaw / 10.f; }
      int rawToPosition(uint16_t raw) const { return raw / 10; }
      JsonDocument* getState(RequestContext&);
    };
  }
  namespace debug {
    struct Rs485 {
      rs485::Stats _stats;
      void* _task = nullptr;
      JsonDocument* getState(RequestContext&);
    };
  }
}

#include "production.inc"

template <class T> JsonDocument state(T& object) {
  esp32m::RequestContext context;
  std::unique_ptr<JsonDocument> doc(object.getState(context));
  return *doc;
}

void fan() {
  esp32m::dev::Fan device;
  assert(state(device)["sampledAtMs"] == -1);
  fake::nowMs = 2000;
  assert(device.pollSensors());
  auto first = state(device);
  assert(first["sampledAtMs"] == 2000 && std::abs(first["rpm"].as<float>() - 3600) < .01);
  fake::nowMs += 123;
  assert(state(device)["sampledAtMs"] == 2000 && state(device)["stamp"].isUnbound());
  fake::nowMs = (uint64_t(1) << 33) + 1000;
  assert(device.pollSensors() && state(device)["sampledAtMs"].as<uint64_t>() == fake::nowMs);
  const auto last = fake::nowMs;
  device.tach.counter.error = -1; fake::nowMs += 500;
  device.pollSensors(); assert(state(device)["sampledAtMs"] == last);
  std::cout << "PASS Fan actual PCNT completion timestamp, unchanged-read silence, failure retention and 64-bit clock\n";
}

void valve() {
  esp32m::dev::FlomarvelValve device;
  auto initial = state(device);
  assert(initial["actualSampledAtMs"] == -1 && initial["targetSampledAtMs"] == -1 &&
         initial["probeSampledAtMs"] == -1 && !initial["actualValid"].as<bool>());
  const uint64_t late = (uint64_t(1) << 33) + 1000;
  device._actualStamp = late; device._targetStamp = late - 100; device._probeStamp = late - 200;
  fake::nowMs = late + 50;
  const auto first = state(device);
  assert(first["actualSampledAtMs"] == late && first["targetSampledAtMs"] == late - 100 &&
         first["probeSampledAtMs"] == late - 200 && first["actualValid"] == true);
  fake::nowMs += 37;
  auto repeated = state(device);
  assert(repeated == first && repeated["actualAge"].isUnbound() &&
         repeated["targetAge"].isUnbound() && repeated["probeAge"].isUnbound());
  fake::nowMs += 5000;
  repeated = state(device);
  assert(repeated["actualSampledAtMs"] == late && repeated["actualValid"] == false &&
         repeated["position"] == -1);
  std::cout << "PASS Flomarvel independent source timestamps, never-read sentinels and unchanged freshness rules\n";
}

void diagnostics() {
  esp32m::debug::Rs485 device;
  const uint64_t late = (uint64_t(1) << 33) + 1000;
  device._stats.reset(late);
  fake::nowMs = late + 100;
  auto empty = state(device);
  assert(empty["lastAttemptAtMs"] == -1 && empty["comparisonStartedAtMs"] == late);
  device._stats.record({false, 5, esp32m::modbus::Outcome::Success, 23000}, late + 150);
  fake::nowMs = late + 2000;
  auto first = state(device);
  assert(first["lastAttemptAtMs"] == late + 150 && first["peers"][0]["lastAttemptAtMs"] == late + 150 &&
         first["comparisonStartedAtMs"] == late && first["windowEndedAtMs"] == fake::nowMs / 1000 * 1000);
  assert(first["lastAgeMs"].isUnbound() && first["elapsedMs"].isUnbound() && first["windowLagMs"].isUnbound());
  assert(first["recent"]["meanMs"] == 23);
  fake::nowMs += 37;
  auto repeated = state(device);
  assert(repeated == first);
  std::cout << "PASS RS485 exact start/completion/window anchors, stable reads and preserved rate/latency durations\n";
}

int main() { fan(); valve(); diagnostics(); }
