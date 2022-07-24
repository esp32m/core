#include "esp32m/bus/owb.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"

#include <string.h>
#include <map>

namespace esp32m {

  namespace owb {

    uint8_t crc(uint8_t c, uint8_t data) {
      // https://www.maximintegrated.com/en/app-notes/index.mvp/id/27
      static const uint8_t table[256] = {
          0,   94,  188, 226, 97,  63,  221, 131, 194, 156, 126, 32,  163, 253,
          31,  65,  157, 195, 33,  127, 252, 162, 64,  30,  95,  1,   227, 189,
          62,  96,  130, 220, 35,  125, 159, 193, 66,  28,  254, 160, 225, 191,
          93,  3,   128, 222, 60,  98,  190, 224, 2,   92,  223, 129, 99,  61,
          124, 34,  192, 158, 29,  67,  161, 255, 70,  24,  250, 164, 39,  121,
          155, 197, 132, 218, 56,  102, 229, 187, 89,  7,   219, 133, 103, 57,
          186, 228, 6,   88,  25,  71,  165, 251, 120, 38,  196, 154, 101, 59,
          217, 135, 4,   90,  184, 230, 167, 249, 27,  69,  198, 152, 122, 36,
          248, 166, 68,  26,  153, 199, 37,  123, 58,  100, 134, 216, 91,  5,
          231, 185, 140, 210, 48,  110, 237, 179, 81,  15,  78,  16,  242, 172,
          47,  113, 147, 205, 17,  79,  173, 243, 112, 46,  204, 146, 211, 141,
          111, 49,  178, 236, 14,  80,  175, 241, 19,  77,  206, 144, 114, 44,
          109, 51,  209, 143, 12,  82,  176, 238, 50,  108, 142, 208, 83,  13,
          239, 177, 240, 174, 76,  18,  145, 207, 45,  115, 202, 148, 118, 40,
          171, 245, 23,  73,  8,   86,  180, 234, 105, 55,  213, 139, 87,  9,
          235, 181, 54,  104, 138, 212, 149, 203, 41,  119, 244, 170, 72,  22,
          233, 183, 85,  11,  136, 214, 52,  106, 43,  117, 151, 201, 74,  20,
          246, 168, 116, 42,  200, 150, 21,  75,  169, 247, 182, 232, 10,  84,
          215, 137, 107, 53};

      return table[c ^ data];
    }

    uint8_t crc(uint8_t c, const uint8_t *buffer, size_t len) {
      do {
        c = crc(c, *buffer++);
      } while (--len > 0);
      return c;
    }

    std::map<gpio_num_t, IDriver*> _drivers;
    std::mutex _driversMutex;

    typedef IDriver *(*rmtDriverCreator)(gpio_num_t pin);
#ifdef ESP32M_USE_DEPRECATED_RMT
    extern IDriver *newDeprecatedRmtDriver(gpio_num_t pin);
    rmtDriverCreator createRmtDriver = newDeprecatedRmtDriver;
#else
    extern IDriver *newRmtDriver(gpio_num_t pin);
    rmtDriverCreator createRmtDriver = newRmtDriver;
#endif

    IDriver *useDriver(gpio_num_t pin) {
      std::lock_guard guard(_driversMutex);
      auto found = _drivers.find(pin);
      if (found == _drivers.end()) {
        auto driver = createRmtDriver(pin);
        if (driver)
          _drivers[pin] = driver;
        return driver;
      }
      return found->second;
      /*
      IDriver *found = nullptr;
      int i;
      for (i = 0; i < MaxDrivers; i++)
        if (_drivers[i] && _drivers[i]->pin() == pin) {
          found = _drivers[i];
          break;
        }
      if (!found)
        for (i = 0; i < MaxDrivers; i++)
          if (!_drivers[i]) {
            found = createRmtDriver(pin, i);
            if (found) {
              _drivers[i] = found;
              break;
            }
          }
      return found;*/
    }

    void IDriver::release() {
      std::lock_guard guard(_driversMutex);
      _refs--;
      if (_refs == 0) {
        _drivers.erase(_pin);
        delete this;
      }
    }
    void IDriver::claim() {
      _refs++;
    }

    char *toString(ROMCode &rom_code, char *buffer, size_t len) {
      for (int i = 0; i < sizeof(rom_code.bytes) && len >= 2; i++) {
        sprintf(buffer, "%02x", rom_code.bytes[i]);
        buffer += 2;
        len -= 2;
      }
      return buffer;
    }

    Search::Search(Owb *owb) : _owb(owb) {}

    ROMCode *Search::next() {
      if ((_started && !_found) || _error)
        return nullptr;
      if (!_started) {
        memset(&_romCode, 0, sizeof(_romCode));
        _lastDiscrepancy = 0;
        _lastFamilyDiscrepancy = 0;
        _lastDeviceFlag = false;
        _started = true;
      }
      _error = search(&_found);
      return _found && !_error ? &_romCode : nullptr;
    }

    esp_err_t Search::search(bool *is_found) {
      // initialize for search
      int id_bit_number = 1;
      int last_zero = 0;
      int rom_byte_number = 0;
      bool id_bit = false;
      bool cmp_id_bit = false;
      uint8_t rom_byte_mask = 1;
      bool search_direction = false;
      bool search_result = false;
      uint8_t crc8 = 0;

      // if the last call was not the last one
      if (!_lastDeviceFlag) {
        // 1-Wire reset
        bool is_present;
        ESP_CHECK_RETURN(_owb->reset(is_present));
        if (!is_present) {
          // reset the search
          _lastDiscrepancy = 0;
          _lastDeviceFlag = false;
          _lastFamilyDiscrepancy = 0;
          *is_found = false;
          return ESP_OK;
        }

        // issue the search command
        ESP_CHECK_RETURN(_owb->write(RomSearch));

        // loop to do the search
        do {
          id_bit = cmp_id_bit = false;

          // read a bit and its complement
          ESP_CHECK_RETURN(_owb->read(id_bit));
          ESP_CHECK_RETURN(_owb->read(cmp_id_bit));

          // check for no devices on 1-wire (signal level is high in both bit
          // reads)
          if (id_bit && cmp_id_bit)
            break;
          else {
            // all devices coupled have 0 or 1
            if (id_bit != cmp_id_bit)
              search_direction = id_bit;  // bit write value for search
            else {
              // if this discrepancy if before the Last Discrepancy
              // on a previous next then pick the same as last time
              if (id_bit_number < _lastDiscrepancy)
                search_direction =
                    ((_romCode.bytes[rom_byte_number] & rom_byte_mask) > 0);
              else
                // if equal to last pick 1, if not then pick 0
                search_direction = (id_bit_number == _lastDiscrepancy);

              // if 0 was picked then record its position in LastZero
              if (!search_direction) {
                last_zero = id_bit_number;

                // check for Last discrepancy in family
                if (last_zero < 9)
                  _lastFamilyDiscrepancy = last_zero;
              }
            }

            // set or clear the bit in the ROM byte rom_byte_number
            // with mask rom_byte_mask
            if (search_direction)
              _romCode.bytes[rom_byte_number] |= rom_byte_mask;
            else
              _romCode.bytes[rom_byte_number] &= ~rom_byte_mask;
            // serial number search direction write bit
            ESP_CHECK_RETURN(_owb->write(search_direction));

            // increment the byte counter id_bit_number
            // and shift the mask rom_byte_mask
            id_bit_number++;
            rom_byte_mask <<= 1;

            // if the mask is 0 then go to new SerialNum byte rom_byte_number
            // and reset mask
            if (rom_byte_mask == 0) {
              crc8 =
                  crc(crc8,
                      _romCode.bytes[rom_byte_number]);  // accumulate the CRC
              rom_byte_number++;
              rom_byte_mask = 1;
            }
          }
        } while (rom_byte_number < 8);  // loop until through all ROM bytes 0-7

        // if the search was successful then
        if (!((id_bit_number < 65) || (crc8 != 0))) {
          // search successful so set
          // LastDiscrepancy,LastDeviceFlag,search_result
          _lastDiscrepancy = last_zero;

          // check for last device
          if (_lastDiscrepancy == 0)
            _lastDeviceFlag = true;

          search_result = true;
        }
      }

      // if no device found then reset counters so next 'search' will be like
      // a first
      if (!search_result || !_romCode.bytes[0]) {
        _lastDiscrepancy = 0;
        _lastDeviceFlag = false;
        _lastFamilyDiscrepancy = 0;
        search_result = false;
      }

      *is_found = search_result;

      return ESP_OK;
    }

  }  // namespace owb

  Owb::Owb(gpio_num_t pin) : _mutex(locks::get(pin)) {
    _driver = owb::useDriver(pin);
    if (_driver)
      _driver->claim();
  }

  Owb::~Owb() {
    if (_driver)
      _driver->release();
  }

  esp_err_t Owb::reset(bool &present) {
    return _driver->reset(present);
  }

  esp_err_t Owb::read(bool &value) {
    return _driver->readBits((uint8_t *)&value, 1);
  }

  esp_err_t Owb::read(uint8_t &value) {
    return _driver->readBits((uint8_t *)&value, 8);
  }

  esp_err_t Owb::read(void *buffer, size_t size) {
    for (int i = 0; i < size; ++i) {
      uint8_t out;
      esp_err_t res = _driver->readBits(&out, 8);
      if (res != ESP_OK)
        return res;
      ((uint8_t *)buffer)[i] = out;
    }
    return ESP_OK;
  }

  esp_err_t Owb::write(bool value) {
    return _driver->writeBits(value ? 1 : 0, 1);
  }

  esp_err_t Owb::write(uint8_t value) {
    return _driver->writeBits(value, 8);
  }

  esp_err_t Owb::write(const void *buffer, size_t size) {
    for (int i = 0; i < size; i++) {
      esp_err_t res = _driver->writeBits(((uint8_t *)buffer)[i], 8);
      if (res != ESP_OK)
        return res;
    }
    return ESP_OK;
  }

  esp_err_t Owb::write(const owb::ROMCode &code) {
    return write(code.bytes, sizeof(code.bytes));
  }

}  // namespace esp32m