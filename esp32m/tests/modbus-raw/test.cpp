#include "esp32m/bus/modbus.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <future>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool installed = false, controller = false, raw = false;
int64_t now = 0;
int writes = 0, requests = 0;
std::string fail;
std::vector<std::string> events;
std::deque<std::vector<uint8_t>> fragments;
std::deque<esp_err_t> requestResults;
std::vector<int64_t> requestTimes;
bool checkQuietLock = false;
bool quantizedDelay = false;
unsigned quietLockChecks = 0;
bool pendingCompletion = false;
void* retainedData = nullptr;
std::vector<uint8_t> replyData, sentData;
uart_config_t uartConfig{};
mb_communication_info_t restored{};
std::mutex uartMutex;
struct Capture : esp32m::modbus::Observer {
  std::vector<esp32m::modbus::Transaction> results;
  void onTransaction(const esp32m::modbus::Transaction& t) override { results.push_back(t); }
} capture;
void event(const std::string& name) { events.push_back(name); }
bool fails(const char* name) { return fail == name; }
bool complete(const uint8_t* bytes, size_t size, void*) {
  return size >= 5 && bytes[size-3] == 0xEB && bytes[size-2] == 0x54 && bytes[size-1] == 0x39;
}
void reset() {
  capture.results.clear();
  fail.clear(); writes = requests = 0; events.clear(); fragments.clear();
  requestResults.clear(); requestTimes.clear(); checkQuietLock = false;
  quietLockChecks = 0; quantizedDelay = false;
  pendingCompletion = false;
  retainedData = nullptr; replyData.clear(); sentData.clear();
}
}
void checkLock() {
  if (checkQuietLock) {
    // Check from a competing task, since try_lock on a mutex owned by the
    // current thread is undefined. Other users cannot occupy the quiet gap.
    auto competingTask = std::async(std::launch::async, [] {
      if (!uartMutex.try_lock()) return false;
      uartMutex.unlock();
      return true;
    });
    assert(!competingTask.get());
    ++quietLockChecks;
  }
}
int64_t esp_timer_get_time() { return now; }
void vTaskDelay(TickType_t ticks) {
  checkLock();
  if (!ticks) return; // FreeRTOS only yields for a zero-tick delay.
  if (quantizedDelay)
    now = (now / 10000 + ticks) * 10000;
  else
    now += int64_t(ticks) * 10000;
  event("ticks" + std::to_string(ticks));
}
namespace esp32m {
void delay(uint32_t ms) {
  if (quantizedDelay) {
    vTaskDelay(pdMS_TO_TICKS(ms));
    return;
  }
  if (ms == 100) checkLock();
  now += int64_t(ms)*1000; event("delay" + std::to_string(ms));
}
namespace locks { std::mutex& uart(uart_port_t) { return uartMutex; } }
}
esp_err_t mbc_master_create_serial(mb_communication_info_t* cfg, void** handle) {
  event("controller-create"); assert(!installed && !controller);
  if (fails("restore")) return ESP_FAIL;
  restored = *cfg; controller = installed = true; *handle = reinterpret_cast<void*>(1); return ESP_OK;
}
esp_err_t mbc_master_set_descriptor(void*, const mb_parameter_descriptor_t*, int) { return ESP_OK; }
esp_err_t mbc_master_start(void*) { event("controller-start"); assert(controller); return ESP_OK; }
esp_err_t mbc_master_stop(void*) { event("controller-stop"); return ESP_OK; }
esp_err_t mbc_master_delete(void*) {
  event("controller-delete"); assert(controller);
  if (fails("controller-delete")) return ESP_FAIL;
  pendingCompletion = false;
  controller = installed = false; return ESP_OK;
}
esp_err_t mbc_master_send_request(void*, mb_param_request_t* req, void* data) {
  ++requests;
  retainedData = data;
  const auto bytes = req->command == esp32m::modbus::ReadCoils ||
                             req->command == esp32m::modbus::ReadDiscrete ||
                             req->command == esp32m::modbus::WriteCoils
                         ? (req->reg_size + 7) / 8 : req->reg_size * 2;
  sentData.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + bytes);
  if (!replyData.empty()) {
    assert(replyData.size() <= size_t(bytes));
    std::memcpy(data, replyData.data(), replyData.size());
  }
  requestTimes.push_back(now);
  // stop/start and UART flush cannot clear a queued Modbus completion.
  // Model a late error retained by the controller until it is destroyed.
  if (pendingCompletion) return ESP_ERR_TIMEOUT;
  if (requestResults.empty()) return ESP_OK;
  auto result = requestResults.front();
  requestResults.pop_front();
  if (result == ESP_ERR_TIMEOUT || result == ESP_ERR_INVALID_RESPONSE)
    pendingCompletion = true;
  return result;
}
esp_err_t uart_set_pin(uart_port_t, int tx, int rx, int, int) { assert(tx == 17 && rx == 16); return ESP_OK; }
esp_err_t uart_set_mode(uart_port_t, int mode) { assert(mode == UART_MODE_RS485_HALF_DUPLEX); return ESP_OK; }
esp_err_t uart_set_rx_timeout(uart_port_t, uint8_t) { return ESP_OK; }
esp_err_t uart_flush_input(uart_port_t) { assert(installed); return ESP_OK; }
esp_err_t uart_param_config(uart_port_t, const uart_config_t* cfg) {
  event("raw-config"); assert(!installed); uartConfig = *cfg;
  return fails("config") ? ESP_FAIL : ESP_OK;
}
esp_err_t uart_driver_install(uart_port_t, int, int, int queue, void*, int) {
  event("raw-install"); assert(!installed && !controller && queue == 0);
  if (fails("install")) return ESP_FAIL;
  raw = installed = true; return ESP_OK;
}
esp_err_t uart_driver_delete(uart_port_t) {
  event("raw-delete"); assert(raw && installed && !controller);
  if (fails("cleanup")) return ESP_FAIL;
  raw = installed = false; return ESP_OK;
}
bool uart_is_driver_installed(uart_port_t) { return installed; }
int uart_write_bytes(uart_port_t, const void*, size_t size) {
  event("write"); ++writes; assert(raw && installed && !controller);
  assert(events[events.size()-2] == "delay150");
  return fails("short-write") ? int(size)-1 : int(size);
}
esp_err_t uart_wait_tx_done(uart_port_t, TickType_t ticks) { assert(ticks); return fails("tx") ? ESP_ERR_TIMEOUT : ESP_OK; }
int uart_read_bytes(uart_port_t, void* out, size_t capacity, TickType_t ticks) {
  assert(raw && capacity == 1 && ticks);
  if (fails("read")) return -1;
  if (fragments.empty()) { now += ticks*10000; return 0; }
  now += 1000;
  auto& bytes = fragments.front();
  auto count = std::min(bytes.size(), capacity);
  std::memcpy(out, bytes.data(), count);
  bytes.erase(bytes.begin(), bytes.begin()+count);
  if (bytes.empty()) fragments.pop_front();
  return count;
}
int main() {
  auto& master = esp32m::modbus::Master::instance();
  master.configureSerial(UART_NUM_2, 38400, UART_PARITY_EVEN);
  assert(master.start() == ESP_OK);
  master.setObserver(&capture);
  reset();
  assert(master.start() == ESP_OK && events.empty());
  const uint8_t tx[] = {0xEB, 0x44}; uint8_t rx[16]; size_t size = 99;
  auto exchange = [&](bool callback = true, size_t capacity = sizeof(rx)) {
    return master.exchangeRaw(tx, sizeof(tx), rx, capacity, &size, 9600, 100,
                              callback ? complete : nullptr);
  };
  reset(); fragments = {{0xFE}, {0x00, 0xEB}, {0x54}, {0x39}};
  assert(exchange() == ESP_OK && size == 5 && writes == 1);
  assert(capture.results.size() == 1 && capture.results[0].raw);
  assert(capture.results[0].outcome == esp32m::modbus::Outcome::Success);
  assert(capture.results[0].durationUs == 5000); // Excludes both 150 ms guards.
  assert(master.isRunning() && controller && installed && !raw);
  assert(uartConfig.baud_rate == 9600 && uartConfig.parity == UART_PARITY_DISABLE);
  assert(uartConfig.data_bits == UART_DATA_8_BITS && uartConfig.stop_bits == UART_STOP_BITS_1);
  assert(restored.ser_opts.baudrate == 38400 && restored.ser_opts.parity == UART_PARITY_EVEN);
  assert(events.front() == "controller-delete");
  assert(std::find(events.begin(), events.end(), "raw-delete") < std::find(events.begin(), events.end(), "controller-create"));
  int data; assert(master.request(1, esp32m::modbus::ReadHolding, 0, 1, &data) == ESP_OK);

  reset(); checkQuietLock = true;
  auto started = now;
  assert(master.request(5, esp32m::modbus::ReadHolding, 0, 1, &data, false, 100) == ESP_OK);
  assert(requests == 1 && requestTimes[0] - started == 100000 && quietLockChecks == 1);
  assert(capture.results.size() == 1 && capture.results[0].address == 5);
  assert(!capture.results[0].raw && capture.results[0].durationUs == 0);
  // The optional guard does not slow down devices that did not request it.
  started = now;
  assert(master.request(10, esp32m::modbus::ReadHolding, 0x100C, 1, &data, false) == ESP_OK);
  assert(now == started && quietLockChecks == 1);
  reset(); checkQuietLock = true; requestResults = {ESP_ERR_TIMEOUT, ESP_OK};
  assert(master.request(5, esp32m::modbus::ReadHolding, 0, 1, &data, true, 100) == ESP_OK);
  assert(requests == 2 && quietLockChecks == 2);
  assert(capture.results.size() == 2);
  assert(capture.results[0].outcome == esp32m::modbus::Outcome::Timeout);
  assert(capture.results[1].outcome == esp32m::modbus::Outcome::Success);
  assert(requestTimes[1] - requestTimes[0] >= 100000);
  reset(); checkQuietLock = true; requestResults = {ESP_ERR_TIMEOUT, ESP_OK};
  assert(master.request(5, esp32m::modbus::WriteRegister, 2, 1, &data, false, 100) == ESP_ERR_TIMEOUT);
  assert(requests == 1 && quietLockChecks == 1 && requestResults.size() == 1);
  assert(capture.results.size() == 1); // Failed single-shot write is not retried.

  // Model the real 100 Hz scheduler: a 12 ms delay becomes one tick and can
  // return almost immediately when called just before the next tick. The
  // bus contract requires actual elapsed silence, independent of tick phase.
  for (const uint32_t quietMs : {12U, 1U, 4U, 100U}) {
    for (const int phaseUs : {0, 1, 5000, 9000, 9999}) {
      reset(); quantizedDelay = checkQuietLock = true;
      now = (now / 10000 + 1) * 10000 + phaseUs;
      started = now;
      assert(master.request(10, esp32m::modbus::ReadHolding, 0x0101,
                            1, &data, false, quietMs) == ESP_OK);
      assert(requests == 1 && quietLockChecks > 0);
      const auto elapsed = requestTimes[0] - started;
      assert(elapsed >= int64_t(quietMs) * 1000);
      assert(elapsed < int64_t(quietMs) * 1000 + 10000);
      assert(capture.results.size() == 1 && capture.results[0].durationUs == 0);
      started = now;
      assert(master.request(10, esp32m::modbus::ReadHolding, 0x0101,
                            1, &data, false) == ESP_OK);
      assert(now == started); // Zero guard must remain zero, without retries.
    }
  }

  for (const auto error : {ESP_ERR_TIMEOUT, ESP_ERR_INVALID_RESPONSE}) {
    reset(); requestResults = {error, ESP_OK};
    assert(master.request(1, esp32m::modbus::WriteRegister, 0x2000, 1, &data, false) == error);
    assert(requests == 1 && master.isRunning());
    assert(std::find(events.begin(), events.end(), "controller-delete") != events.end());
    assert(std::find(events.begin(), events.end(), "controller-create") != events.end());
    assert(std::find(events.begin(), events.end(), "controller-start") != events.end());
    assert(master.request(1, esp32m::modbus::ReadHolding, 0x100C, 1, &data) == ESP_OK);
    assert(requests == 2);
    reset(); requestResults = {error, ESP_OK};
    assert(master.request(1, esp32m::modbus::ReadHolding, 0, 1, &data) == ESP_OK);
    assert(requests == 2);
  }
  // A failed final retry must also leave a clean controller for the NEXT slave.
  reset(); requestResults = {ESP_ERR_TIMEOUT, ESP_ERR_INVALID_RESPONSE, ESP_OK};
  assert(master.request(3, esp32m::modbus::ReadHolding, 3, 1, &data) == ESP_ERR_INVALID_RESPONSE);
  assert(requests == 2 && !pendingCompletion);
  assert(std::count(events.begin(), events.end(), "controller-delete") == 2);
  assert(master.request(5, esp32m::modbus::ReadHolding, 0, 1, &data, false, 100) == ESP_OK);
  assert(requests == 3);

  reset(); fail = "controller-delete"; requestResults = {ESP_ERR_TIMEOUT, ESP_OK};
  data = 0xCAFE;
  assert(master.request(3, esp32m::modbus::ReadHolding, 3, 1, &data) == ESP_ERR_TIMEOUT);
  assert(requests == 1 && requestResults.size() == 1);
  // A callback can still arrive after an API timeout and failed teardown.
  // It must never hold a pointer into a driver's expired stack frame.
  assert(retainedData != &data);
  *static_cast<uint16_t*>(retainedData) = 0;
  assert(data == 0xCAFE);
  assert(master.request(5, esp32m::modbus::ReadHolding, 0, 1, &data, false) != ESP_OK);
  assert(requests == 1); // Do not reuse a controller whose teardown failed.
  fail.clear();
  assert(master.start() == ESP_OK && !pendingCompletion);

  reset(); fail = "restore"; requestResults = {ESP_ERR_TIMEOUT, ESP_OK};
  assert(master.request(3, esp32m::modbus::ReadHolding, 3, 1, &data) == ESP_ERR_TIMEOUT);
  assert(requests == 1 && !master.isRunning() && !controller);
  reset();
  assert(master.request(5, esp32m::modbus::ReadHolding, 0, 1, &data, false) == ESP_OK);
  assert(master.isRunning());
  reset(); data = 0xCAFE; replyData = {0x34, 0x12};
  assert(master.request(5, esp32m::modbus::ReadHolding, 0, 1, &data, false) == ESP_OK);
  assert(data == 0x1234 && retainedData != &data);
  reset(); data = 0xCAFE; replyData = {0, 0}; requestResults = {ESP_ERR_TIMEOUT};
  assert(master.request(5, esp32m::modbus::ReadHolding, 0, 1, &data, false) == ESP_ERR_TIMEOUT);
  assert(data == 0xCAFE); // Never publish partial data on a failed transfer.
  reset(); data = 0x1234;
  assert(master.request(5, esp32m::modbus::WriteRegister, 2, 1, &data, false) == ESP_OK);
  assert((sentData == std::vector<uint8_t>{0x34, 0x12}));
  reset(); uint8_t bits[] = {0xFF, 0xFF, 0xA5}; replyData = {0x55, 0x01};
  assert(master.request(5, esp32m::modbus::ReadCoils, 0, 9, bits, false) == ESP_OK);
  assert(bits[0] == 0x55 && bits[1] == 1 && bits[2] == 0xA5);
  reset();
  assert(master.request(5, esp32m::modbus::ReadHolding, 0, 257, &data, false) == ESP_ERR_INVALID_SIZE);
  assert(master.request(5, esp32m::modbus::ReadHolding, 0, 1, nullptr, false) == ESP_ERR_INVALID_ARG);
  assert(requests == 0);
  assert(capture.results.empty()); // Invalid API input is not bus traffic.
  reset(); requestResults = {ESP_FAIL, ESP_OK};
  assert(master.request(1, esp32m::modbus::WriteRegister, 0x2000, 1, &data, false) == ESP_FAIL);
  assert(requests == 1 && events.empty());

  reset(); fragments = {{0xFE}};
  assert(exchange() == ESP_ERR_TIMEOUT && size == 1 && writes == 1 && master.isRunning());
  assert(capture.results.size() == 1 && capture.results[0].outcome == esp32m::modbus::Outcome::Timeout);
  reset(); fragments = {{1,2,3,4}};
  assert(exchange(true, 4) == ESP_ERR_INVALID_SIZE && size == 4 && writes == 1 && master.isRunning());
  assert(capture.results[0].outcome == esp32m::modbus::Outcome::InvalidResponse);
  reset(); fragments = {{1,2,3}};
  assert(exchange(false) == ESP_OK && size == 3 && writes == 1);
  assert(capture.results[0].outcome == esp32m::modbus::Outcome::Unverified);
  for (const char* failure : {"config", "install", "short-write", "tx", "read"}) {
    reset(); fail = failure;
    assert(exchange() != ESP_OK && master.isRunning() && installed && controller && !raw);
    assert(writes <= 1);
  }
  reset(); fail = "controller-delete";
  assert(exchange() == ESP_FAIL && writes == 0 && master.isRunning());
  assert(capture.results.size() == 1 && capture.results[0].outcome == esp32m::modbus::Outcome::OtherError);
  reset(); fail = "restore";
  assert(exchange() == ESP_FAIL && !master.isRunning() && !installed && !controller && !raw);
  reset(); installed = true; // Unowned UART driver must not be configured/deleted.
  assert(exchange() == ESP_ERR_INVALID_STATE && writes == 0 && events.empty());
  installed = false;
  reset(); fragments = {{0xFE,0,0xEB,0x54,0x39}};
  assert(exchange() == ESP_OK && !master.isRunning() && !installed && !controller && !raw);
  assert(master.start() == ESP_OK);
  reset(); fail = "cleanup";
  assert(exchange() == ESP_FAIL && !controller && installed && raw && !master.isRunning());
  assert(std::find(events.begin(), events.end(), "controller-create") == events.end());
  fail.clear(); assert(uart_driver_delete(UART_NUM_2) == ESP_OK);
  assert(master.start() == ESP_OK);
  reset();
  assert(master.exchangeRaw(tx, sizeof(tx), rx, 0, &size) == ESP_ERR_INVALID_ARG && size == 0 && events.empty());
  assert(capture.results.empty());
  assert(master.request(0, esp32m::modbus::WriteRegister, 1, 1, &data, false) == ESP_OK);
  assert(capture.results.size() == 1 && capture.results[0].outcome == esp32m::modbus::Outcome::Unverified);
  master.setObserver(nullptr);
  std::cout << "modbus raw: fragmented/noisy frames, deadlines, overflow, one TX, cleanup, configuration restoration, optional request retry and locked quiet intervals passed\n";
}
