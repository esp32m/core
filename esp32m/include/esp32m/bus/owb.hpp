#pragma once

#include <mutex>

#include <esp_err.h>
#include <hal/gpio_types.h>

#include "esp32m/defs.hpp"

namespace esp32m {

  class Owb;

  namespace owb {

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

    class IDriver {
     public:
      IDriver(gpio_num_t pin) : _pin(pin) {}
      virtual ~IDriver() {}
      virtual esp_err_t reset(bool &present) = 0;
      virtual esp_err_t readBits(uint8_t *in, int number_of_bits_to_read) = 0;
      virtual esp_err_t writeBits(uint8_t out, int number_of_bits_to_write) = 0;
      void claim();
      void release();
      gpio_num_t pin() {
        return _pin;
      }

     private:
      gpio_num_t _pin;
      int _refs = 0;
    };

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
      esp_err_t search(bool *is_found);
      friend class esp32m::Owb;
    };

    IDriver *useRmtDriver(gpio_num_t pin);

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
    owb::IDriver *_driver;
    std::mutex &_mutex;
    friend class owb::Search;
  };

}  // namespace esp32m