#include <esp_flash.h>
#include <esp_flash_partitions.h>
#include <esp_partition.h>

#include "esp32m/debug/partitions.hpp"

namespace esp32m {
  namespace debug {

    Partitions &Partitions::instance() {
      static Partitions i;
      return i;
    }

    DynamicJsonDocument *Partitions::getState(const JsonVariantConst args) {
      const uint32_t *ptr;
      spi_flash_mmap_handle_t handle;
      esp_err_t err = ESP_ERROR_CHECK_WITHOUT_ABORT(spi_flash_mmap(
          ESP_PARTITION_TABLE_OFFSET & 0xffff0000, SPI_FLASH_SEC_SIZE,
          SPI_FLASH_MMAP_DATA, (const void **)&ptr, &handle));
      if (err != ESP_OK)
        return nullptr;
      const esp_partition_info_t *start =
          (const esp_partition_info_t *)(ptr +
                                         (ESP_PARTITION_TABLE_OFFSET & 0xffff) /
                                             sizeof(*ptr));
      const esp_partition_info_t *end =
          start + SPI_FLASH_SEC_SIZE / sizeof(*start);
      int pc = 0;
      for (const esp_partition_info_t *it = start; it != end; ++it)
        if (it->magic != ESP_PARTITION_MAGIC)
          break;
        else
          pc++;
      DynamicJsonDocument *doc =
          new DynamicJsonDocument(JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(pc) +
                                  pc * (16 + JSON_ARRAY_SIZE(6)));
      auto root = doc->to<JsonObject>();
      auto partitions = root.createNestedArray("partitions");
      for (const esp_partition_info_t *it = start; it != end; ++it)
        if (it->magic != ESP_PARTITION_MAGIC)
          break;
        else {
          auto e = partitions.createNestedArray();
          e.add((char *)it->label);
          e.add(it->type);
          e.add(it->subtype);
          e.add(it->pos.offset);
          e.add(it->pos.size);
          e.add(it->flags);
        }

      spi_flash_munmap(handle);
      return doc;
    }

    Partitions *usePartitions() {
      return &Partitions::instance();
    }

  }  // namespace debug
}  // namespace esp32m