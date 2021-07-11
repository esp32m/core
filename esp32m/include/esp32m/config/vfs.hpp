#pragma once

#include <esp_vfs.h>

#include "esp32m/config/store.hpp"

namespace esp32m {

  class ConfigVfs : public ConfigStore {
   public:
    ConfigVfs(const char* path) : _path(path) {}
    ConfigVfs(const ConfigVfs&) = delete;
    const char* name() const override;

   protected:
    void write(const DynamicJsonDocument& config) override;
    DynamicJsonDocument* read() override;
    void reset() override;

   private:
    const char* _path;
    bool check(bool ok, FILE* stream, const char* msg);
    size_t read(char** buf, size_t* mu);
    void dump();
  };

}  // namespace esp32m