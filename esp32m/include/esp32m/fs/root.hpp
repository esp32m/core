#pragma once

#include <esp_err.h>
#include <cstddef>
#include <functional>
#include <string>

namespace esp32m::fs {
  struct RootInfo {
    std::string backend, label;
    size_t total = 0, used = 0;
    bool ready = false;
  };

  // Custom pre-mounted roots can supply the same capabilities as the built-in
  // wrappers. The query must not emit log messages (the logger consumes it).
  using RootQuery = std::function<bool(size_t& total, size_t& used)>;
  void registerRoot(const char* backend, const char* label, RootQuery query);
  RootInfo rootInfo();

  // The persistent log store closes writers/readers before an explicit format.
  // Refuse the format if those resources cannot be quiesced in bounded time.
  void setRootFormatHooks(std::function<bool()> before,
                          std::function<void(bool)> after);
  esp_err_t formatRoot(const std::function<esp_err_t()>& formatter);
}  // namespace esp32m::fs
