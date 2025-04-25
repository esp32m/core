#pragma once

#include <esp_vfs.h>

#include "esp32m/config/config.hpp"

namespace esp32m {
  namespace config {

    class Vfs : public Store {
     public:
      Vfs(const char* path) : _path(path), _backup(path) {
        _backup += ".bak";
      }
      Vfs(const Vfs&) = delete;
      const char* name() const override {
        return "config-vfs";
      }

     protected:
      void write(const JsonDocument& config) override;
      JsonDocument* read() override;
      void reset() override;

     private:
      std::string _path, _backup;
      uint32_t _crc = 0;
      bool check(bool ok, FILE* stream, const char* msg);
      // size_t read(char** buf, size_t* mu);
      void dump();
    };
  }  // namespace config

}  // namespace esp32m