#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../src/debug/stack-monitor.cpp"

#if CONFIG_ESP32M_STACK_MONITOR

namespace {
  std::vector<TaskStatus_t> tasks;
  std::vector<std::string> warnings;
  std::map<void*, size_t> allocations;
  size_t allocationCalls = 0;
  size_t failAllocationAt = 0;
  unsigned snapshotCalls = 0;
  unsigned snapshotFailures = 0;
  UBaseType_t reportedCount = 0;
  bool inSnapshot = false;
  int64_t clockUs = 0;

  TaskStatus_t task(unsigned number, uint32_t free, uintptr_t handle = 0) {
    // Borrowed name and stack pointers are deliberately inaccessible. Any
    // post-snapshot dereference in production code must fail this test.
    return {reinterpret_cast<void*>(handle ? handle : number),
            reinterpret_cast<const char*>(1), number,
            reinterpret_cast<StackType_t*>(1), free};
  }

  void reset() {
    assert(allocations.empty());
    tasks = {task(1, 2048)};
    warnings.clear();
    allocationCalls = failAllocationAt = 0;
    snapshotCalls = snapshotFailures = reportedCount = 0;
    inSnapshot = false;
    clockUs = 0;
  }

  void expectWarning(size_t index, const char* fragment) {
    assert(index < warnings.size());
    if (warnings[index].find(fragment) == std::string::npos) {
      std::fprintf(stderr, "Expected %s in %s\n", fragment, warnings[index].c_str());
      std::abort();
    }
  }

  void crossingAndMaterialLows() {
    reset();
    esp32m::debug::Monitor monitor;
    tasks.push_back(task(2, 513));
    monitor.poll(0);
    assert(warnings.empty());
    tasks[1].usStackHighWaterMark = 512;
    monitor.poll(29999);
    assert(snapshotCalls == 1 && warnings.empty());
    monitor.poll(30000);
    assert(warnings.size() == 1);
    expectWarning(0, "task #2");
    expectWarning(0, "minimum free 512 bytes");
    tasks[1].usStackHighWaterMark = 460;
    monitor.poll(60000);
    assert(warnings.size() == 1);
    tasks[1].usStackHighWaterMark = 384;
    monitor.poll(90000);
    assert(warnings.size() == 2);
    tasks[1].usStackHighWaterMark = 100;
    monitor.poll(120000);
    tasks[1].usStackHighWaterMark = 0;
    monitor.poll(150000);
    assert(warnings.size() == 4);
    expectWarning(3, "minimum free 0 bytes");
    monitor.poll(180000);
    assert(warnings.size() == 4);
  }

  void taskDeletionAndHandleReuse() {
    reset();
    esp32m::debug::Monitor monitor;
    tasks.push_back(task(2, 400, 20));
    monitor.poll(0);
    tasks[1] = task(3, 400, 20);
    monitor.poll(30000);
    assert(warnings.size() == 2);
    expectWarning(1, "task #3");
    tasks.pop_back();
    monitor.poll(60000);
    // A vanished identity is reclaimed; retained state is bounded by the live
    // snapshot, even if a synthetic test reuses both opaque identity fields.
    tasks.push_back(task(3, 400, 20));
    monitor.poll(90000);
    assert(warnings.size() == 3);
  }

  void warningBudgetDefersWithoutDropping() {
    reset();
    esp32m::debug::Monitor monitor;
    for (unsigned i = 2; i <= 7; ++i) tasks.push_back(task(i, 400));
    monitor.poll(0);
    assert(warnings.size() == 4);
    monitor.poll(30000);
    assert(warnings.size() == 6);
    monitor.poll(60000);
    assert(warnings.size() == 6);
  }

  void capacityRaceAndRetry() {
    reset();
    esp32m::debug::Monitor monitor;
    reportedCount = 1;  // 9 slots, while the actual snapshot needs 20.
    for (unsigned i = 2; i <= 20; ++i) tasks.push_back(task(i, 2048));
    monitor.poll(0);
    assert(snapshotCalls == 2 && warnings.empty());
    snapshotFailures = 2;
    monitor.poll(30000);
    assert(snapshotCalls == 4 && warnings.size() == 1);
    expectWarning(0, "task set changed during snapshot");
    monitor.poll(60000);
    assert(warnings.size() == 1);
  }

  void allocationFailureKeepsHistory() {
    reset();
    esp32m::debug::Monitor monitor;
    tasks.push_back(task(2, 400));
    monitor.poll(0);
    const auto originalBlocks = allocations.size();
    for (unsigned i = 3; i <= 12; ++i) tasks.push_back(task(i, 2048));
    failAllocationAt = allocationCalls + 2;  // second replacement allocation
    monitor.poll(30000);
    assert(allocations.size() == originalBlocks);
    assert(warnings.size() == 2);
    expectWarning(1, "snapshot allocation failed");
    failAllocationAt = 0;
    monitor.poll(60000);
    assert(warnings.size() == 2);  // existing low task is not warned twice
    tasks[1].usStackHighWaterMark = 272;
    monitor.poll(90000);
    assert(warnings.size() == 3);
    expectWarning(2, "minimum free 272 bytes");
  }

  void failedSnapshotsDoNotEraseHistory() {
    reset();
    esp32m::debug::Monitor monitor;
    tasks.push_back(task(2, 400));
    monitor.poll(0);
    snapshotFailures = 2;
    monitor.poll(30000);
    monitor.poll(60000);
    assert(warnings.size() == 2);
    expectWarning(1, "sample unavailable");
  }

  void taskLimitAndFailureRateLimit() {
    reset();
    esp32m::debug::Monitor monitor;
    for (unsigned i = 2; i <= 33; ++i) tasks.push_back(task(i, 2048));
    monitor.poll(0);
    assert(warnings.size() == 1 && snapshotCalls == 0 && allocations.empty());
    expectWarning(0, "task count exceeds monitor limit");
    monitor.poll(30000);
    monitor.poll(299999);
    assert(warnings.size() == 1);
    monitor.poll(330000);
    assert(warnings.size() == 2);
    tasks.resize(1);
    monitor.poll(360000);
    assert(snapshotCalls == 1);
  }

  void boundedRetainedMemory() {
    reset();
    esp32m::debug::Monitor monitor;
    for (unsigned i = 2; i <= 32; ++i) tasks.push_back(task(i, 2048));
    monitor.poll(0);
    size_t bytes = 0;
    for (const auto& allocation : allocations) bytes += allocation.second;
    assert(bytes == 32 * (sizeof(TaskStatus_t) + sizeof(esp32m::debug::Entry)));
    const auto calls = allocationCalls;
    for (unsigned sample = 1; sample <= 100; ++sample) {
      tasks.resize(1);
      tasks.push_back(task(100 + sample, 2048));
      monitor.poll(sample * 30000ULL);
    }
    assert(allocationCalls == calls && warnings.empty());
  }
}  // namespace

void* pvPortMalloc(size_t size) {
  assert(!inSnapshot);
  if (++allocationCalls == failAllocationAt) return nullptr;
  auto* pointer = std::malloc(size);
  assert(pointer);
  allocations[pointer] = size;
  return pointer;
}

void vPortFree(void* pointer) {
  assert(!inSnapshot);
  if (!pointer) return;
  assert(allocations.erase(pointer) == 1);
  std::free(pointer);
}

UBaseType_t uxTaskGetNumberOfTasks() {
  return reportedCount ? reportedCount : UBaseType_t(tasks.size());
}

UBaseType_t uxTaskGetSystemState(TaskStatus_t* output, UBaseType_t capacity,
                               uint32_t* runtime) {
  assert(runtime == nullptr);
  ++snapshotCalls;
  inSnapshot = true;
  UBaseType_t count = 0;
  if (snapshotFailures) --snapshotFailures;
  else if (capacity >= tasks.size()) {
    for (const auto& item : tasks) output[count++] = item;
  }
  // A fresh task-count read sees the changed set after a capacity race.
  reportedCount = 0;
  inSnapshot = false;
  return count;
}

int64_t esp_timer_get_time() { return clockUs; }

void testWarning(const char* format, ...) {
  assert(!inSnapshot);
  char text[512];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(text, sizeof(text), format, arguments);
  va_end(arguments);
  warnings.emplace_back(text);
}

int main() {
  crossingAndMaterialLows();
  taskDeletionAndHandleReuse();
  warningBudgetDefersWithoutDropping();
  capacityRaceAndRetry();
  allocationFailureKeepsHistory();
  failedSnapshotsDoNotEraseHistory();
  taskLimitAndFailureRateLimit();
  boundedRetainedMemory();
  assert(allocations.empty());
  std::puts("stack-monitor: 8 scenario groups passed");
}

#else

int main() {
  // The disabled API must link without any FreeRTOS, timer or logger symbols.
  esp32m::debug::pollStackMonitor();
  std::puts("stack-monitor: disabled build passed");
}

#endif
