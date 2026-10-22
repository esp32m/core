#include "esp32m/fs/root.hpp"

#include <mutex>

namespace esp32m::fs {
  namespace {
    std::mutex stateMutex, formatMutex;
    std::string backend, label;
    RootQuery capacity;
    std::function<bool()> beforeFormat;
    std::function<void(bool)> afterFormat;
  }

  void registerRoot(const char* name, const char* partition, RootQuery query) {
    std::lock_guard guard(stateMutex);
    backend = name ? name : "";
    label = partition ? partition : "";
    capacity = std::move(query);
  }

  RootInfo rootInfo() {
    RootInfo info;
    RootQuery query;
    {
      std::lock_guard guard(stateMutex);
      info.backend = backend;
      info.label = label;
      query = capacity;
    }
    info.ready = query && query(info.total, info.used) &&
                 info.total >= info.used;
    return info;
  }

  void setRootFormatHooks(std::function<bool()> before,
                          std::function<void(bool)> after) {
    std::lock_guard guard(stateMutex);
    beforeFormat = std::move(before);
    afterFormat = std::move(after);
  }

  esp_err_t formatRoot(const std::function<esp_err_t()>& formatter) {
    std::unique_lock operation(formatMutex, std::try_to_lock);
    if (!operation.owns_lock()) return ESP_ERR_INVALID_STATE;
    std::function<bool()> before;
    std::function<void(bool)> after;
    {
      std::lock_guard guard(stateMutex);
      before = beforeFormat;
      after = afterFormat;
    }
    if (before && !before()) return ESP_ERR_TIMEOUT;
    const auto result = formatter();
    if (after) after(result == ESP_OK);
    return result;
  }
}  // namespace esp32m::fs
