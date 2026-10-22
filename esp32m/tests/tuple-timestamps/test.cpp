#include <ArduinoJson.h>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include "stamps.inc"
using std::isnan;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expression) (expression)
#define ESP_CHECK_RETURN_BOOL(expression) do { if ((expression) != ESP_OK) return false; } while (0)
int64_t nowMs = 0;
int64_t esp_timer_get_time() { return nowMs * 1000; }
uint32_t millis() { return static_cast<uint32_t>(nowMs); }
int esp_task_wdt_add(void*) { return 0; }
int esp_task_wdt_reset() { return 0; }
void delay(int) { throw std::runtime_error("one worker step"); }
namespace esp32m {
  struct RequestContext {};
  namespace config { struct Changed { static void publish(void*) {} }; }
  struct Sensor {
    JsonDocument doc;
    uint64_t sampledAt = 0;
    unsigned calls = 0;
    void set(float value, bool* changed = nullptr) { doc.set(value); ++calls; if (changed) *changed = true; }
    void setSample(float value, uint64_t stamp) { set(value); sampledAt = stamp; }
    JsonVariantConst get() const { return doc.as<JsonVariantConst>(); }
  };
  struct Adc {
    int value = 500, result = ESP_OK;
    int read(int& raw) { raw = value; return result; }
  };
  struct Counter { int value = 6, result = ESP_OK; int read(int& pc, bool) { pc = value; return result; } };
  struct Pin { Counter counter; Counter* pcnt() { return &counter; } };
  struct Sampler {
    std::mutex guard;
    float value = NAN;
    std::mutex& mutex() { return guard; }
    void add(void* raw) { std::lock_guard lock(guard); value = *static_cast<int*>(raw); }
    float avg() { return value; }
    float kvar() { return 0; }
  };
  namespace modbus {
    enum class Command { ReadHolding };
    struct Master {
      bool running = true;
      int result = ESP_OK;
      int16_t decimals = 1, value = 123;
      static Master& instance() { static Master master; return master; }
      bool isRunning() { return running; }
      int request(uint8_t, Command, int, int, int16_t* values) {
        values[0] = decimals; values[1] = value; return result;
      }
    };
  }
  namespace dev {
    struct Fc37 {
      JsonDocument* getState(RequestContext&);
      bool pollSensors();
      Adc adc; Adc* _adc = &adc;
      int _divisor = 1000; float _value = NAN;
      STAMP_fc37
      Sensor _sensor;
    };
    struct Ptmb {
      JsonDocument* getState(RequestContext&);
      bool pollSensors();
      uint8_t _addr = 1; float _value = 0;
      STAMP_ptmb
      Sensor _sensor;
    };
    struct Cap11xx {
      JsonDocument* getState(RequestContext&);
      int result = ESP_FAIL;
      int getTouchBitmap(uint8_t& bits) { bits = 7; return result; }
      STAMP_cap11xx
    };
    struct Mlx90614 {
      JsonDocument* getState(RequestContext&);
      bool pollSensors();
      struct Bus { int address() { return 90; } } bus;
      Bus* _i2c = &bus;
      float ambient = 20, object = 30; bool fail = false;
      unsigned reads = 0;
      int getAmbientTemperature(float& value) { ++reads; value = ambient; return fail ? ESP_FAIL : ESP_OK; }
      int getObjectTemperature(float& value) { ++reads; value = object; return fail ? ESP_FAIL : ESP_OK; }
      Sensor _ambientTemperature, _objectTemperature;
      STAMP_mlx90614
    };
    struct FlowSensor {
      JsonDocument* getState(RequestContext&);
      bool pollSensors();
      float compute(int, int);
      Pin pin; Pin* _pin = &pin;
      float _value = NAN, _consumption = 0, _dumpedConsumption = 0;
      STAMP_flow
      uint32_t _flowStampMs = 0, _lastDump = 0; // ESP32 unsigned-long ABI
      Sensor _sensorFlow, _sensorConsumption;
    };
    struct MicrowaveMotionSensor {
      JsonDocument* getState(RequestContext&);
      bool pollSensors(); void run();
      Adc adc; Adc* _adc = &adc;
      Sampler sampler; Sampler* _sampler = &sampler;
      int _divisor = 1000;
      STAMP_mwms
      std::mutex _sampleMutex;
      Sensor _motion;
      void workerStep() { try { run(); } catch (const std::runtime_error&) {} }
    };
  }
}
#include "production.inc"
using namespace esp32m::dev;
template <class T> JsonDocument state(T& device) {
  esp32m::RequestContext context;
  std::unique_ptr<JsonDocument> result(device.getState(context)); return *result;
}
int main() {
  Fc37 adc; nowMs = 1234; assert(state(adc)[0] == -1);
  assert(adc.pollSensors()); assert(state(adc)[0] == 1234);
  nowMs += 60000; assert(state(adc)[0] == 1234); // serialization is not acquisition
  adc.adc.result = ESP_FAIL; assert(adc.pollSensors()); assert(state(adc)[0] == 1234);
  adc.adc.result = ESP_OK; nowMs = 0; assert(adc.pollSensors()); assert(state(adc)[0] == 0);
  nowMs = (int64_t(1) << 33) + 17; assert(adc.pollSensors()); assert(state(adc)[0] == nowMs);
  nowMs += 1000; assert(adc.pollSensors()); assert(state(adc)[0] == nowMs); // equal value, new sample
  std::cout << "PASS ADC never-read, zero, rollover, failed and unchanged successful acquisitions\n";
  Ptmb pressure; auto& mb = esp32m::modbus::Master::instance();
  assert(state(pressure)[0] == -1); assert(pressure.pollSensors()); auto stamp = nowMs;
  mb.decimals = 99; nowMs += 2000; assert(pressure.pollSensors()); assert(state(pressure)[0] == stamp);
  mb.result = ESP_FAIL; assert(!pressure.pollSensors()); assert(state(pressure)[0] == stamp);
  std::cout << "PASS Modbus skipped/invalid-format polls do not manufacture sample time\n";
  Cap11xx touch; assert(state(touch)[0] == -1); touch.result = ESP_OK;
  nowMs = 0; assert(state(touch)[0] == 0); nowMs = (int64_t(1) << 34) + 9;
  assert(state(touch)[0] == nowMs); touch.result = ESP_FAIL; nowMs += 1000;
  assert(state(touch)[0] == nowMs - 1000);
  Mlx90614 infrared; assert(state(infrared)[0] == -1 && infrared.reads == 0);
  assert(infrared.pollSensors()); stamp = nowMs; auto reads = infrared.reads;
  nowMs += 5000; assert(state(infrared)[0] == stamp && infrared.reads == reads);
  std::cout << "PASS demand acquisition timestamp and cached infrared snapshot provenance\n";
  FlowSensor flow; nowMs = (int64_t(1) << 32) - 1000;
  flow._flowStampMs = static_cast<uint32_t>(nowMs); nowMs += 2000;
  assert(flow.pollSensors()); assert(state(flow)[0] == nowMs);
  assert(std::abs(state(flow)[1].as<float>() - (3.0f + 8) / 6) < 0.0001f);
  std::cout << "PASS pulse-counter acquisition time while legacy interval crosses32-bit rollover\n";
  MicrowaveMotionSensor motion; assert(state(motion)[0] == -1);
  motion.workerStep(); assert(motion.pollSensors()); stamp = nowMs;
  nowMs += 60000; motion.adc.result = ESP_FAIL; motion.workerStep();
  assert(motion.pollSensors()); assert(state(motion)[0] == stamp && motion._motion.sampledAt == uint64_t(stamp));
  nowMs += 1000; motion.adc.result = ESP_OK; motion.workerStep();
  assert(motion.pollSensors()); assert(state(motion)[0] == nowMs);
  std::cout << "PASS background sampler acquisition provenance survives cached republish and ADC failure\n";
}
