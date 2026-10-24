#pragma once

#include <ArduinoJson.h>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace esp32m::ui {
  // Immutable, shared wire bytes. One checked allocation owns both bytes and
  // reference count; copying a queued/fanned-out payload never allocates.
  class Payload {
   public:
    // Custom allocators must return malloc/free-compatible storage.
    using Allocate = void* (*)(size_t);
    Payload() noexcept = default;
    Payload(const Payload& other) noexcept : _storage(other._storage) { retain(); }
    Payload(Payload&& other) noexcept : _storage(other._storage) {
      other._storage = nullptr;
    }
    Payload& operator=(Payload other) noexcept {
      std::swap(_storage, other._storage);
      return *this;
    }
    ~Payload() { release(); }
    explicit operator bool() const noexcept { return _storage != nullptr; }
    const char* data() const noexcept {
      return _storage ? reinterpret_cast<const char*>(_storage + 1) : nullptr;
    }
    const char* c_str() const noexcept { return data(); }
    const char* get() const noexcept { return data(); }
    size_t size() const noexcept { return _storage ? _storage->size : 0; }
    size_t capacity() const noexcept { return size(); }

    template <class Fill>
    static Payload build(size_t size, Fill&& fill, Allocate allocate = std::malloc) {
      if (!allocate || size > std::numeric_limits<size_t>::max() - sizeof(Storage) - 1)
        return {};
      auto* memory = allocate(sizeof(Storage) + size + 1);
      if (!memory) return {};
      Payload result;
      result._storage = new (memory) Storage(size);
      auto* bytes = reinterpret_cast<char*>(result._storage + 1);
      if (!fill(bytes, size)) return {};
      bytes[size] = 0;
      return result;
    }
    static Payload copy(const char* text, size_t size,
                        Allocate allocate = std::malloc) {
      if (!text && size) return {};
      return build(size, [&](char* destination, size_t) {
        if (size) std::memcpy(destination, text, size);
        return true;
      }, allocate);
    }
    static Payload copy(const char* text, Allocate allocate = std::malloc) {
      return copy(text, text ? std::strlen(text) : 0, allocate);
    }
    static Payload fromJson(JsonVariantConst value, Allocate allocate = std::malloc) {
      const auto size = measureJson(value);
      return build(size, [&](char* destination, size_t length) {
        return serializeJson(value, destination, length + 1) == length;
      }, allocate);
    }

   private:
    struct Storage {
      std::atomic<size_t> references{1};
      const size_t size;
      explicit Storage(size_t size) : size(size) {}
    };
    Storage* _storage = nullptr;
    void retain() noexcept {
      if (_storage) _storage->references.fetch_add(1, std::memory_order_relaxed);
    }
    void release() noexcept {
      if (_storage && _storage->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        _storage->~Storage();
        std::free(_storage);
      }
    }
  };
}  // namespace esp32m::ui
