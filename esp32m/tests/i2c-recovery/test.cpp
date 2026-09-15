#include "esp32m/bus/i2c/master.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;
using esp32m::i2c::MasterBus;
using esp32m::i2c::MasterDev;

namespace fake {
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<std::string> events;
  std::deque<esp_err_t> transferResults, resetResults;
  std::string block;
  bool blocked = false, release = false, overlap = false;
  unsigned active = 0;
  std::atomic<unsigned long> now{10000};

  void clear() {
    std::lock_guard lock(mutex);
    events.clear();
    transferResults.clear();
    resetResults.clear();
    block.clear();
    blocked = release = overlap = false;
    active = 0;
  }
  esp_err_t operation(const std::string& name, bool transfer = false) {
    std::unique_lock lock(mutex);
    if (++active > 1) overlap = true;
    events.push_back(name);
    if (block == name) {
      block.clear();
      blocked = true;
      changed.notify_all();
      changed.wait(lock, [] { return release; });
    }
    auto& results = name == "reset" ? resetResults : transferResults;
    esp_err_t result = ESP_OK;
    if ((transfer || name == "reset") && !results.empty()) {
      result = results.front();
      results.pop_front();
    }
    --active;
    return result;
  }
  void waitBlocked() {
    std::unique_lock lock(mutex);
    if (!changed.wait_for(lock, 2s, [] { return blocked; }))
      throw std::runtime_error("SDK call did not reach test barrier");
  }
  void unblock() {
    std::lock_guard lock(mutex);
    release = true;
    changed.notify_all();
  }
  void event(const std::string& value) {
    std::lock_guard lock(mutex);
    events.push_back(value);
  }
}

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*, i2c_master_bus_handle_t* bus) {
  *bus = new FakeBus;
  return ESP_OK;
}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus) { delete bus; return ESP_OK; }
esp_err_t i2c_master_probe(i2c_master_bus_handle_t, uint16_t, int) { return fake::operation("probe"); }
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t) { return fake::operation("reset"); }
esp_err_t i2c_master_bus_wait_all_done(i2c_master_bus_handle_t, int) { return fake::operation("wait"); }
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev) {
  auto result = fake::operation("remove");
  if (result == ESP_OK) delete dev;
  return result;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t*, i2c_master_dev_handle_t* dev) {
  auto result = fake::operation("add");
  if (result == ESP_OK) *dev = new FakeDevice{bus};
  return result;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t*, size_t, uint8_t* data, size_t size, int) {
  auto result = fake::operation("read-reg", true);
  if (result == ESP_OK) std::memset(data, 0x23, size);
  return result;
}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t, uint8_t* data, size_t size, int) {
  auto result = fake::operation("read", true);
  if (result == ESP_OK) std::memset(data, 0x34, size);
  return result;
}
esp_err_t i2c_master_multi_buffer_transmit(i2c_master_dev_handle_t, i2c_master_transmit_multi_buffer_info_t*, size_t, int) { return fake::operation("write-multi", true); }
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t*, size_t, int) { return fake::operation("write", true); }
void vTaskDelay(TickType_t ticks) { fake::event("delay:" + std::to_string(ticks)); }
namespace esp32m {
  unsigned long millis() { return fake::now; }
  void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
  std::string string_printf(const char* format, ...) {
    char buf[128];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return buf;
  }
}

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void expect(std::initializer_list<const char*> expected) {
  std::vector<std::string> values(expected.begin(), expected.end());
  require(fake::events == values, "unexpected SDK/retry call order");
}

// The first SDK operation is held in progress while another bus user starts.
// Observe the shared lock directly as well as call ordering and overlap.
void exclude(MasterBus& bus, const std::string& blocked,
             const std::function<void()>& first, const std::function<void()>& second) {
  fake::clear();
  fake::block = blocked;
  auto owner = std::async(std::launch::async, first);
  fake::waitBlocked();
  bool locked = bus.mutex().try_lock();
  if (locked) bus.mutex().unlock();
  std::promise<void> started;
  auto waiter = std::async(std::launch::async, [&] { started.set_value(); second(); });
  started.get_future().wait();
  const bool escaped = waiter.wait_for(30ms) == std::future_status::ready;
  fake::unblock();
  owner.get();
  waiter.get();
  require(!locked, "active SDK operation does not hold bus lock");
  require(!escaped && !fake::overlap, "SDK operations overlapped on one bus");
}

int main() {
  try {
    std::unique_ptr<MasterBus> bus(MasterBus::getDefault());
    std::unique_ptr<MasterDev> dev(MasterDev::create(0x20, bus.get()));
    std::unique_ptr<MasterDev> other(MasterDev::create(0x21, bus.get()));
    require(dev && other, "device creation failed");
    uint8_t value = 0;

    fake::clear();
    fake::transferResults = {ESP_ERR_TIMEOUT, ESP_OK};
    require(dev->read(0, value) == ESP_OK, "timeout did not recover");
    expect({"read-reg", "reset", "delay:1", "read-reg"});

    fake::clear();
    fake::transferResults = {ESP_ERR_TIMEOUT, ESP_ERR_TIMEOUT, ESP_ERR_TIMEOUT, ESP_ERR_TIMEOUT};
    require(dev->read(0, value) == ESP_ERR_TIMEOUT, "retry limit swallowed failure");
    expect({"read-reg", "reset", "delay:1", "read-reg", "reset", "delay:1",
            "read-reg", "reset", "delay:1", "read-reg"});

    fake::clear();
    fake::transferResults = {ESP_ERR_TIMEOUT};
    fake::resetResults = {ESP_ERR_INVALID_STATE};
    require(dev->read(0, value) == ESP_ERR_INVALID_STATE, "reset failure was ignored");
    expect({"read-reg", "reset"});

    for (auto error : {ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_FAIL}) {
      fake::clear();
      fake::transferResults = {error};
      require(dev->write(0, value) == error, "non-timeout write error was replayed");
      expect({"write-multi"});
    }
    dev->setRetryInvalidResponse(true);
    require(dev->getRetryInvalidResponse() && !other->getRetryInvalidResponse(), "retry policy leaked between devices");
    fake::clear();
    fake::transferResults = {ESP_ERR_INVALID_RESPONSE, ESP_OK};
    require(dev->write(0, value) == ESP_OK, "opt-in invalid-response write retry failed");
    expect({"write-multi", "reset", "delay:1", "write-multi"});
    fake::clear();
    fake::transferResults = {ESP_ERR_INVALID_RESPONSE};
    require(other->read(0, value) == ESP_ERR_INVALID_RESPONSE, "default device gained optional retry policy");
    expect({"read-reg"});
    dev->setRetryInvalidResponse(false);

    for (auto retries : {0, -10}) {
      dev->setRetries(retries);
      require(dev->getRetries() == 0, "negative retry count not clamped");
      fake::clear();
      fake::transferResults = {ESP_ERR_TIMEOUT};
      require(dev->read(0, value) == ESP_ERR_TIMEOUT, "zero retries did not execute one attempt");
      expect({"read-reg"});
    }
    dev->setRetries(3);

    fake::clear();
    require(dev->read(nullptr, 0, &value, 1) == ESP_OK, "plain receive failed");
    require(dev->write(nullptr, 0, &value, 1) == ESP_OK, "data write failed");
    require(dev->write(&value, 1, nullptr, 0) == ESP_OK, "register write failed");
    require(dev->write(nullptr, 0, nullptr, 0) == ESP_ERR_INVALID_ARG, "empty write accepted");
    expect({"read", "write", "write"});

    fake::clear();
    MasterDev* found = nullptr;
    require(bus->find(0x20, &found) == ESP_OK && found == dev.get(), "device registry lookup failed");
    require(bus->find(0x22) == ESP_ERR_NOT_FOUND, "missing device was found");

    exclude(*bus, "read-reg", [&] { require(dev->read(0, value) == ESP_OK, "read failed"); },
            [&] { require(bus->reset() == ESP_OK, "reset failed"); });
    expect({"read-reg", "reset"});
    exclude(*bus, "reset", [&] { require(bus->reset() == ESP_OK, "reset failed"); },
            [&] { uint8_t v; require(other->read(0, v) == ESP_OK, "second-device read failed"); });
    expect({"reset", "read-reg"});
    exclude(*bus, "probe", [&] { require(bus->probe(0x20) == ESP_OK, "probe failed"); },
            [&] { require(bus->reset() == ESP_OK, "reset failed"); });
    expect({"probe", "reset"});
    exclude(*bus, "add", [&] { std::unique_ptr<MasterDev> added(MasterDev::create(0x22, bus.get())); },
            [&] { require(bus->reset() == ESP_OK, "reset failed"); });
    require(!fake::overlap, "device addition overlapped reset");
    exclude(*bus, "remove", [&] { other.reset(); },
            [&] { require(bus->reset() == ESP_OK, "reset failed"); });
    expect({"wait", "remove", "reset"});

    // Hold recovery itself while another device tries to enter: retry and its
    // positive tick backoff must complete before the waiting transfer.
    other.reset(MasterDev::create(0x21, bus.get()));
    fake::clear();
    fake::block = "reset";
    fake::transferResults = {ESP_ERR_TIMEOUT, ESP_OK, ESP_OK};
    auto recovery = std::async(std::launch::async, [&] { return dev->read(0, value); });
    fake::waitBlocked();
    auto waiting = std::async(std::launch::async, [&] { uint8_t v; return other->read(1, v); });
    const bool escaped = waiting.wait_for(30ms) == std::future_status::ready;
    fake::unblock();
    require(recovery.get() == ESP_OK && waiting.get() == ESP_OK, "concurrent recovery failed");
    require(!escaped && !fake::overlap, "transfer overlapped recovery");
    expect({"read-reg", "reset", "delay:1", "read-reg", "read-reg"});

    // Endianness and mutable configuration can be used inside a multi-call
    // bus lock, which requires the lock to be recursive.
    {
      std::lock_guard lock(dev->mutex());
      dev->setTimeout(40);
      dev->setEndianness(esp32m::Little);
      require(dev->getTimeout() == 40 && dev->getEndianness() == esp32m::Little, "settings mismatch");
      uint16_t word = 0;
      require(dev->read(0, word) == ESP_OK && word == 0x2323, "typed read failed");
      require(dev->write(0, word) == ESP_OK, "typed write failed");
    }
    dev->setRetries(0);
    fake::now = 20000;
    for (int i = 0; i < 5; ++i) {
      fake::transferResults = {ESP_ERR_TIMEOUT};
      require(dev->read(0, value) == ESP_ERR_TIMEOUT, "failure accounting input failed");
    }
    require(!dev->isResponsive(), "communication failure statistics not updated");
    require(dev->read(0, value) == ESP_OK && dev->isResponsive(), "successful recovery did not restore responsiveness");

    other.reset();
    dev.reset();
    bus.reset();
    std::cout << "PASS: production I2C retry policy, bounded attempts, nonzero backoff, reset failures, bus exclusion and lifecycle\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
