#pragma once

#include <mutex>

#include <esp_err.h>
#include <hal/gpio_types.h>

#include "esp32m/defs.hpp"

namespace esp32m {

  class Owb;

  namespace owb {
    class Driver;

    static const uint8_t RomSearch = 0xF0;
    static const uint8_t RomRead = 0x33;
    static const uint8_t RomMatch = 0x55;
    static const uint8_t RomSkip = 0xCC;

    typedef union {
      /// Provides access via field names
      struct fields {
        uint8_t
            family[1];  ///< family identifier (1 byte, LSB - read/write first)
        uint8_t serial_number[6];  ///< serial number (6 bytes)
        uint8_t crc[1];  ///< CRC check byte (1 byte, MSB - read/write last)
      } fields;          ///< Provides access via field names

      uint8_t bytes[8];  ///< Provides raw byte access

    } ROMCode;

    char *toString(ROMCode &rom_code, char *buffer, size_t len);
    uint8_t crc(uint8_t c, uint8_t data);
    uint8_t crc(uint8_t c, const uint8_t *buffer, size_t len);

    class Search {
     public:
      Search(Owb *owb);
      ROMCode *next();
      esp_err_t error() const {
        return _error;
      }

     private:
      Owb *_owb;
      bool _found = false, _started = false;
      ROMCode _romCode = {};
      int _lastDiscrepancy = 0;
      int _lastFamilyDiscrepancy = 0;
      int _lastDeviceFlag = 0;
      esp_err_t _error = ESP_OK;
      friend class esp32m::Owb;
      friend class Driver;
    };

  }  // namespace owb

  class Owb {
   public:
    Owb(gpio_num_t pin);
    Owb(const Owb &) = delete;
    ~Owb();
    std::mutex &mutex() {
      return _mutex;
    }
    esp_err_t reset(bool &present);
    esp_err_t read(bool &value);
    esp_err_t read(uint8_t &value);
    esp_err_t read(void *buffer, size_t size);
    esp_err_t write(bool value);
    esp_err_t write(uint8_t value);
    esp_err_t write(const void *buffer, size_t size);
    esp_err_t write(const owb::ROMCode &code);

   private:
    owb::Driver *_driver;
    std::mutex &_mutex;
    friend class owb::Search;
  };

}  // namespace esp32m