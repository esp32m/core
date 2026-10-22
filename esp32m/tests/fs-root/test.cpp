#include "esp32m/fs/root.hpp"
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

using namespace esp32m::fs;
int main() {
  assert(!rootInfo().ready);
  registerRoot("custom", "partition", [](size_t& total, size_t& used) {
    total = 1000000; used = 200000; return true;
  });
  const auto info = rootInfo();
  assert(info.ready && info.backend == "custom" && info.label == "partition");
  assert(info.total == 1000000 && info.used == 200000);
  int formats = 0, completed = 0;
  setRootFormatHooks([] { return false; }, [&](bool) { ++completed; });
  assert(formatRoot([&] { ++formats; return ESP_OK; }) == ESP_ERR_TIMEOUT);
  assert(formats == 0 && completed == 0);
  setRootFormatHooks([] { return rootInfo().ready; }, [&](bool success) {
    assert(rootInfo().ready); completed += success ? 1 : 10;
  });
  assert(formatRoot([&] { ++formats; return ESP_OK; }) == ESP_OK);
  assert(formats == 1 && completed == 1);
  assert(formatRoot([] { return 9; }) == 9 && completed == 11);
  std::mutex mutex;
  std::condition_variable condition;
  bool entered = false, release = false;
  setRootFormatHooks([&] {
    std::unique_lock lock(mutex);
    entered = true; condition.notify_all();
    condition.wait(lock, [&] { return release; });
    return true;
  }, {});
  std::thread first([&] { assert(formatRoot([] { return ESP_OK; }) == ESP_OK); });
  {
    std::unique_lock lock(mutex);
    condition.wait(lock, [&] { return entered; });
  }
  assert(formatRoot([] { assert(false); return ESP_OK; }) == ESP_ERR_INVALID_STATE);
  {
    std::lock_guard lock(mutex); release = true; condition.notify_all();
  }
  first.join();
  registerRoot("invalid", nullptr, [](size_t& total, size_t& used) {
    total = 1; used = 2; return true;
  });
  assert(!rootInfo().ready);
  std::cout << "Root storage: capacity, failed quiescence, success/failure hooks, reentrancy and concurrent format gates passed\n";
}
