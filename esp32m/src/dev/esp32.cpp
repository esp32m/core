#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32
#  include <esp32/rom/ets_sys.h>
#endif
#include <esp_app_format.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_spiffs.h>
#include <esp_system.h>
#include <math.h>
#include <soc/rtc.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/sens_reg.h>

#include "esp32m/dev/esp32.hpp"

namespace esp32m {
  namespace dev {

#if CONFIG_IDF_TARGET_ESP32
    float temperatureReadFixed() {
      SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_FORCE_XPD_SAR, 3,
                        SENS_FORCE_XPD_SAR_S);
      SET_PERI_REG_BITS(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_CLK_DIV, 10,
                        SENS_TSENS_CLK_DIV_S);
      CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
      CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
      SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP_FORCE);
      SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
      ets_delay_us(100);
      SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
      ets_delay_us(5);
      int res = GET_PERI_REG_BITS2(SENS_SAR_SLAVE_ADDR3_REG, SENS_TSENS_OUT,
                                   SENS_TSENS_OUT_S);
      return (res - 32) / 1.8;
    }
#endif
    Esp32::Esp32() {
      esp_image_header_t fhdr;
      auto res = ESP_ERROR_CHECK_WITHOUT_ABORT(esp_flash_read(
          nullptr, (uint32_t *)&fhdr, CONFIG_BOOTLOADER_OFFSET_IN_FLASH,
          sizeof(esp_image_header_t)));
      // logger.dump(log::Level::Debug, &fhdr, sizeof(esp_image_header_t));
      if (res == ESP_OK && fhdr.magic == ESP_IMAGE_HEADER_MAGIC) {
        switch (fhdr.spi_size & 0x0F) {
          case ESP_IMAGE_FLASH_SIZE_1MB:  // 8 MBit (1MB)
            _flashChipSize = 8;
            break;
          case ESP_IMAGE_FLASH_SIZE_2MB:  // 16 MBit (2MB)
            _flashChipSize = 16;
            break;
          case ESP_IMAGE_FLASH_SIZE_4MB:  // 32 MBit (4MB)
            _flashChipSize = 32;
            break;
          case ESP_IMAGE_FLASH_SIZE_8MB:  // 64 MBit (8MB)
            _flashChipSize = 64;
            break;
          case ESP_IMAGE_FLASH_SIZE_16MB:  // 128 MBit (16MB)
            _flashChipSize = 128;
            break;
          case ESP_IMAGE_FLASH_SIZE_32MB:
            _flashChipSize = 256;
            break;
          case ESP_IMAGE_FLASH_SIZE_64MB:
            _flashChipSize = 512;
            break;
          case ESP_IMAGE_FLASH_SIZE_128MB:
            _flashChipSize = 1024;
            break;
          default:  // fail?
            break;
        }
        switch (fhdr.spi_speed & 0x0F) {
          case 0x0:  // 40 MHz
            _flashChipSpeed = 40;
            break;
          case 0x1:  // 26 MHz
            _flashChipSpeed = 26;
            break;
          case 0x2:  // 20 MHz
            _flashChipSpeed = 20;
            break;
          case 0xf:  // 80 MHz
            _flashChipSpeed = 80;
            break;
          default:  // fail?
            break;
        }
        _flashChipMode = fhdr.spi_mode;
      }
      multi_heap_info_t info;
      heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
      _psramSize = info.total_free_bytes + info.total_allocated_bytes;
      _rr = esp_reset_reason();
    }

    DynamicJsonDocument *Esp32::getState(const JsonVariantConst args) {
      size_t spiffsTotal = 0, spiffsUsed = 0;
      if (esp_spiffs_mounted(NULL))
        esp_spiffs_info(NULL, &spiffsTotal, &spiffsUsed);

      auto doc = new DynamicJsonDocument(
          JSON_OBJECT_SIZE(1 + 4)    // heap: size, free, min, max
          + JSON_OBJECT_SIZE(1 + 8)  // chip: model, rev, cores, features, freq,
                                     // mac, temperature, rr
          + JSON_OBJECT_SIZE(1 + 3)  // flash: size, speed, mode
          + (_psramSize ? JSON_OBJECT_SIZE(1 + 4)
                        : 0)  // psram: size, free, min, max
          + (spiffsTotal ? JSON_OBJECT_SIZE(1 + 2) : 0)  // spiffs: size, free
      );
      JsonObject info = doc->to<JsonObject>();

      auto infoHeap = info.createNestedObject("heap");
      multi_heap_info_t heap;
      heap_caps_get_info(&heap, MALLOC_CAP_INTERNAL);

      infoHeap["size"] = heap.total_free_bytes + heap.total_allocated_bytes;
      infoHeap["free"] = heap.total_free_bytes;
      infoHeap["min"] = heap.minimum_free_bytes;
      infoHeap["max"] = heap.largest_free_block;

      auto infoChip = info.createNestedObject("chip");
      auto infoFlash = info.createNestedObject("flash");
      if (_psramSize) {
        auto infoPsram = info.createNestedObject("psram");
        multi_heap_info_t psram;
        heap_caps_get_info(&psram, MALLOC_CAP_SPIRAM);
        infoPsram["size"] = _psramSize;
        infoPsram["free"] = psram.total_free_bytes;
        infoPsram["min"] = psram.minimum_free_bytes;
        infoPsram["max"] = psram.largest_free_block;
      }
      if (spiffsTotal) {
        auto spiffs = info.createNestedObject("spiffs");
        spiffs["size"] = spiffsTotal;
        spiffs["free"] = spiffsTotal - spiffsUsed;
      }
      esp_chip_info_t chip_info;
      esp_chip_info(&chip_info);
      rtc_cpu_freq_config_t conf;
      rtc_clk_cpu_freq_get_config(&conf);
      infoChip["model"] = chip_info.model;
      infoChip["revision"] = chip_info.revision;
      infoChip["cores"] = chip_info.cores;
      infoChip["features"] = chip_info.features;
      infoChip["freq"] = conf.freq_mhz;
      uint64_t _chipmacid = 0LL;
      esp_efuse_mac_get_default((uint8_t *)(&_chipmacid));
      infoChip["mac"] = _chipmacid;
      infoChip["rr"] = _rr;
#if CONFIG_IDF_TARGET_ESP32
      auto temp = temperatureReadFixed();
      if (!isnan(temp))
        infoChip["temperature"] = temp;
#endif
      if (_flashChipSize)
        infoFlash["size"] = _flashChipSize;
      if (_flashChipSpeed)
        infoFlash["speed"] = _flashChipSpeed;
      if (_flashChipMode != 0xff)
        infoFlash["mode"] = (int)_flashChipMode;

      return doc;
    }

    bool Esp32::setConfig(const JsonVariantConst cfg,
                          DynamicJsonDocument **result) {
      bool changed = false;
      auto pm = cfg["pm"];
      if (pm) {
        json::from(pm[0], _pm.max_freq_mhz, 240, &changed);
        json::from(pm[1], _pm.min_freq_mhz, 160, &changed);
        json::from(pm[2], _pm.light_sleep_enable, false, &changed);
        if (changed)
          json::checkSetResult(
              ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pm_configure(&_pm)), result);
      }
      return changed;
    }

    DynamicJsonDocument *Esp32::getConfig(RequestContext &ctx) {
      auto doc = new DynamicJsonDocument(
          JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(3)  // pm
      );
      auto root = doc->to<JsonObject>();
      auto pm = root.createNestedArray("pm");
      pm.add(_pm.max_freq_mhz);
      pm.add(_pm.min_freq_mhz);
      pm.add(_pm.light_sleep_enable);
      return doc;
    }

    Esp32 &Esp32::instance() {
      static Esp32 i;
      return i;
    }

    void useEsp32() {
      Esp32::instance();
    }
  }  // namespace dev
}  // namespace esp32m