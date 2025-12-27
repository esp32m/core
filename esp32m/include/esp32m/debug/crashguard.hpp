#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_core_dump.h"
#include "esp_partition.h"

#include "esp32m/defs.hpp"
#include "esp32m/ui.hpp"
#include "esp32m/ui/asset.hpp"

namespace esp32m {
  namespace debug {

    class CoreDump : public log::Loggable, public ui::ReadableAsset {
     public:
      CoreDump(const CoreDump&) = delete;
      const char* name() const override {
        return "core-dump";
      }
      static CoreDump& instance() {
        static CoreDump instance;
        return instance;
      }
      bool isAvailable() const {
        return _info.size > 0;
      }
      std::unique_ptr<stream::Reader> createReader() const override {
        if (_info.size == 0)
          return nullptr;
        return std::make_unique<Reader>(_info.part, _info.addr, _info.size);
      }

     private:
      class Reader : public stream::Reader {
       public:
        Reader(const esp_partition_t* part, size_t addr, size_t size)
            : _part(part), _addr(addr), _size(size), _offset(0) {}
        size_t read(uint8_t* buf, size_t size) override {
          if (_offset >= _size)
            return 0;
          if (_offset + size > _size)
            size = _size - _offset;
          esp_err_t err = esp_partition_read(
              _part, _addr - _part->address + _offset, buf, size);
          if (err != ESP_OK)
            return 0;
          _offset += size;
          return size;
        }

       private:
        const esp_partition_t* _part;
        size_t _addr;
        size_t _size;
        size_t _offset;
      };
      struct {
        const esp_partition_t* part;
        size_t addr;
        size_t size;
      } _info;

      CoreDump() {
        ui::AssetInfo info{"/debug/core-dump", "application/octet-stream",
                           nullptr, nullptr};
        init(info);
        if (detect() == ESP_OK)
          Ui::instance().addAsset(this);
      }

      esp_err_t detect() {
        memset(&_info, 0, sizeof(_info));

        const esp_partition_t* part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
            NULL /* label */);

        if (!part)
          // No coredump partition in the partition table.
          return ESP_ERR_NOT_FOUND;

        // Quick validity check. If you only want to know whether "something" is
        // there, you could skip this call and just use
        // esp_core_dump_image_get().
        auto err = esp_core_dump_image_check();
        if (err != ESP_OK)
          return err;

        size_t addr = 0, size = 0;
        ESP_CHECK_RETURN(esp_core_dump_image_get(&addr, &size));

        // Sanity: make sure image lives inside the partition.
        if (addr < part->address)
          return ESP_ERR_INVALID_SIZE;

        const size_t rel = addr - part->address;
        if (rel + size > part->size)
          return ESP_ERR_INVALID_SIZE;

        _info.part = part;
        _info.addr = addr;
        _info.size = size;
        logI("core dump found at 0x%zx, size %zu", addr, size);
        return ESP_OK;
      }
    };

    class CrashGuard : public AppObject {
     public:
      CrashGuard(const CrashGuard&) = delete;
      const char* name() const override {
        return "crash-guard";
      }
      static CrashGuard& instance() {
        static CrashGuard instance;
        return instance;
      }

     private:
      CrashGuard() {
        CoreDump::instance();
      }
    };
  }  // namespace debug

}  // namespace esp32m
