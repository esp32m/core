#include "esp32m/debug/stack-monitor.hpp"

#if CONFIG_ESP32M_STACK_MONITOR

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdint>
#include <limits>

#include "esp32m/logging.hpp"

namespace esp32m::debug {
  namespace {
    constexpr size_t MaxTasks = CONFIG_ESP32M_STACK_MONITOR_MAX_TASKS;
    constexpr uint64_t IntervalMs =
        uint64_t(CONFIG_ESP32M_STACK_MONITOR_INTERVAL_SECONDS) * 1000;
    constexpr uint64_t FailureIntervalMs = 5 * 60 * 1000;
    constexpr unsigned MaxWarningsPerSample = 4;
    constexpr uint32_t WarnFreeBytes = CONFIG_ESP32M_STACK_MONITOR_WARN_FREE_BYTES;
    constexpr uint32_t WarnDeltaBytes = CONFIG_ESP32M_STACK_MONITOR_WARN_DELTA_BYTES;
    static_assert(MaxTasks > 0 && IntervalMs > 0 && WarnDeltaBytes > 0);
    static_assert(MaxTasks <= std::numeric_limits<UBaseType_t>::max());
    // Both supported ESP-IDF ports report the low-water mark in bytes.
    static_assert(sizeof(StackType_t) == 1);

    struct Entry {
      // Opaque identity only. Never pass a saved handle to a task API.
      TaskHandle_t handle;
      UBaseType_t number;
      uint32_t warnedFreeBytes;
      bool warned;
      bool seen;
    };

    class Monitor {
     public:
      ~Monitor() {
        vPortFree(_snapshot);
        vPortFree(_entries);
      }

      void poll(uint64_t now) {
        if (_sampled && now - _sampledAt < IntervalMs) return;
        _sampled = true;
        _sampledAt = now;

        UBaseType_t count = 0;
        // A task can be created between the initial count and the snapshot.
        // Retry once with fresh capacity, without holding a scheduler lock
        // across allocations or logging.
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
          const auto wanted = size_t(uxTaskGetNumberOfTasks());
          if (wanted > MaxTasks) {
            failure(now, "task count exceeds monitor limit");
            return;
          }
          const auto nextCapacity = std::min(MaxTasks,
              std::max(wanted + std::min<size_t>(8, MaxTasks - wanted),
                       attempt ? std::min(MaxTasks, _capacity * 2) : _capacity));
          if (!reserve(nextCapacity)) {
            failure(now, "snapshot allocation failed");
            return;
          }
          count = uxTaskGetSystemState(_snapshot, UBaseType_t(_capacity), nullptr);
          if (count) break;
        }
        if (!count) {
          failure(now, "task set changed during snapshot");
          return;
        }

        for (size_t i = 0; i < _entryCount; ++i) _entries[i].seen = false;
        // Reclaim vanished identities before adding new ones. A recycled TCB
        // address has a different task number and must get a fresh warning.
        for (size_t i = 0; i < _entryCount; ++i)
          for (UBaseType_t j = 0; j < count; ++j)
            if (same(_entries[i], _snapshot[j])) {
              _entries[i].seen = true;
              break;
            }
        size_t kept = 0;
        for (size_t i = 0; i < _entryCount; ++i)
          if (_entries[i].seen) _entries[kept++] = _entries[i];
        _entryCount = kept;

        unsigned warnings = 0;
        for (UBaseType_t i = 0; i < count; ++i) {
          const auto& task = _snapshot[i];
          Entry* entry = nullptr;
          for (size_t j = 0; j < _entryCount; ++j)
            if (same(_entries[j], task)) {
              entry = &_entries[j];
              break;
            }
          if (!entry) {
            // Every retained entry belongs to this complete snapshot, so at
            // most count entries can be present, and count <= _capacity.
            entry = &_entries[_entryCount++];
            *entry = {task.xHandle, task.xTaskNumber, 0, false, true};
          }
          const auto free = uint32_t(task.usStackHighWaterMark);
          const auto materiallyLower = entry->warned &&
              free < entry->warnedFreeBytes &&
              (entry->warnedFreeBytes - free >= WarnDeltaBytes || free == 0);
          if (free > WarnFreeBytes || (entry->warned && !materiallyLower) ||
              warnings >= MaxWarningsPerSample)
            continue;

          // TaskStatus_t.pcTaskName is borrowed TCB storage. Another core may
          // delete a task immediately after uxTaskGetSystemState returns. Use
          // copied scalar fields only, with no name/stack/handle dereference.
          logw("stack monitor: task #%lu (%p) minimum free %lu bytes "
               "(warning threshold %lu bytes)",
               static_cast<unsigned long>(task.xTaskNumber),
               static_cast<void*>(task.xHandle), static_cast<unsigned long>(free),
               static_cast<unsigned long>(WarnFreeBytes));
          entry->warned = true;
          entry->warnedFreeBytes = free;
          ++warnings;
        }
      }

     private:
      static bool same(const Entry& entry, const TaskStatus_t& task) {
        return entry.handle == task.xHandle && entry.number == task.xTaskNumber;
      }

      bool reserve(size_t capacity) {
        if (capacity <= _capacity) return true;
        // Allocate both replacements first. On failure the previous successful
        // sample and its warning history remain intact.
        auto* snapshot = static_cast<TaskStatus_t*>(
            pvPortMalloc(capacity * sizeof(TaskStatus_t)));
        auto* entries = static_cast<Entry*>(pvPortMalloc(capacity * sizeof(Entry)));
        if (!snapshot || !entries) {
          vPortFree(snapshot);
          vPortFree(entries);
          return false;
        }
        for (size_t i = 0; i < _entryCount; ++i) entries[i] = _entries[i];
        vPortFree(_snapshot);
        vPortFree(_entries);
        _snapshot = snapshot;
        _entries = entries;
        _capacity = capacity;
        return true;
      }

      void failure(uint64_t now, const char* reason) {
        if (_failureReported && now - _failureAt < FailureIntervalMs) return;
        _failureReported = true;
        _failureAt = now;
        logw("stack monitor: sample unavailable: %s (limit %lu tasks)", reason,
             static_cast<unsigned long>(MaxTasks));
      }

      TaskStatus_t* _snapshot = nullptr;
      Entry* _entries = nullptr;
      size_t _capacity = 0;
      size_t _entryCount = 0;
      uint64_t _sampledAt = 0;
      uint64_t _failureAt = 0;
      bool _sampled = false;
      bool _failureReported = false;
    };
  }  // namespace

  void pollStackMonitor() {
    static Monitor monitor;
    monitor.poll(esp_timer_get_time() / 1000ULL);
  }
}  // namespace esp32m::debug

#endif  // CONFIG_ESP32M_STACK_MONITOR
