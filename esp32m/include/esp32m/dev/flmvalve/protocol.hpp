#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esp32m::dev::flmvalve::protocol {
  constexpr uint8_t ConfigureAddressFunction = 0x20;
  constexpr uint8_t ConfigureBaudrateFunction = 0x30;

  struct Frame {
    std::array<uint8_t, 13> bytes{};
    size_t size = 0;
  };

  struct Reply {
    size_t offset = 0;
    size_t size = 0;
  };

  inline uint16_t crc16(const uint8_t* data, size_t size) {
    uint16_t crc = 0xffff;
    while (size--) {
      crc ^= *data++;
      for (int bit = 0; bit < 8; bit++)
        crc = crc & 1 ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return crc;
  }

  inline void finish(Frame& frame) {
    const auto crc = crc16(frame.bytes.data(), frame.size - 2);
    frame.bytes[frame.size - 2] = crc & 0xff;
    frame.bytes[frame.size - 1] = crc >> 8;
  }

  inline Frame configureAddress(uint8_t currentAddress, uint8_t address) {
    Frame frame;
    frame.size = 11;
    frame.bytes = {currentAddress, ConfigureAddressFunction, 0x00, 0x20,
                   0x00,           0x01,                     0x02, 0x00,
                   address};
    finish(frame);
    return frame;
  }

  inline Frame configureBaudrate(uint8_t currentAddress, uint32_t baud) {
    Frame frame;
    frame.size = 13;
    frame.bytes = {
        currentAddress,
        ConfigureBaudrateFunction,
        0x00,
        0x30,
        0x00,
        0x01,
        0x04,
        static_cast<uint8_t>(baud >> 24),
        static_cast<uint8_t>(baud >> 16),
        static_cast<uint8_t>(baud >> 8),
        static_cast<uint8_t>(baud),
    };
    finish(frame);
    return frame;
  }

  inline bool validCrc(const uint8_t* frame, size_t size) {
    if (!frame || size < 4)
      return false;
    const auto crc = crc16(frame, size - 2);
    return frame[size - 2] == (crc & 0xff) &&
           frame[size - 1] == (crc >> 8);
  }

  /** Locate a complete configuration response, allowing discarded UART noise
   * before the frame. Both the normal fixed-size reply and a Modbus-style
   * five-byte exception reply are recognized.
   */
  inline bool findReply(const uint8_t* bytes, size_t size, uint8_t address,
                        uint8_t function, size_t normalSize, Reply& reply) {
    if (!bytes)
      return false;
    for (size_t offset = 0; offset < size; offset++) {
      if (bytes[offset] != address || offset + 2 > size)
        continue;
      const auto foundFunction = bytes[offset + 1];
      const size_t frameSize = foundFunction == function
                                   ? normalSize
                                   : foundFunction == (function | 0x80) ? 5 : 0;
      if (!frameSize || offset + frameSize > size)
        continue;
      if (!validCrc(bytes + offset, frameSize))
        continue;
      reply = {offset, frameSize};
      return true;
    }
    return false;
  }
}  // namespace esp32m::dev::flmvalve::protocol
