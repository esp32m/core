#pragma once

// Application protocol for the audited SM10 firmware. No bootloader commands.
// Wire layouts and limits: docs/oem/SM-10H/SM-10H-API.md.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace esp32m::dev::sm10::protocol {

using Tuple = std::array<uint8_t, 4>;
using Address = std::array<uint8_t, 7>;
constexpr uint32_t MaxCoarse = 99999999;
constexpr double TotalStepM3 = 0.0001;
constexpr Tuple DiscoveryTuple{0x12, 0x34, 0x56, 0x78};

enum class Command : uint8_t {
  HistoryHourly = 0x40,
  HistoryDaily = 0x41,
  HistoryMonthly = 0x42,
  HistoryYearly = 0x43,
  SetRtc = 0x44,
  Status = 0x80,
  SetConfigTotal = 0x85,
  Fine = 0x8b,
  Identity = 0x8c,
};

enum class ParseResult { Invalid, Incomplete, Complete };

struct Frame {
  std::array<uint8_t, 256> bytes{};
  size_t size = 0;
};

struct EbReply {
  Command request = Command::Identity;
  Tuple tuple{};
  std::array<uint8_t, 202> payload{};
  size_t size = 0;
};

struct Identity {
  Tuple tuple{};
  std::array<uint8_t, 3> upper{};
  std::array<uint8_t, 5> signature{};
  uint8_t status = 0;
};

struct Rtc {
  int year = 0, month = 0, day = 0;
  int hour = 0, minute = 0, second = 0, weekday = 0;
  std::array<uint8_t, 7> raw{};
  bool valid = false;
};

struct Fine {
  uint32_t coarse = 0, total = 0;
  uint16_t fraction = 0, captureCount = 0, voltageMv = 0;
  uint8_t multiplier = 0;
  double volumeM3 = 0, temperatureC = 0;
};

struct Status {
  uint32_t coarse = 0;
  uint16_t intervalMs = 0, bootCount = 0, voltageMv = 0;
  uint8_t status = 0;
  double temperatureC = 0;
};

struct History {
  std::array<uint32_t, 48> coarse{};
  size_t count = 0;
  bool hasCurrent = false;
  uint32_t currentCoarse = 0;
  // Hourly responses contain the current six calendar bytes, not timestamps
  // of the history entries. The unavailable weekday is derived on decoding.
  Rtc currentRtc{};
};

inline uint8_t sum8(const uint8_t *data, size_t size) {
  uint8_t result = 0;
  for (size_t i = 0; i < size; ++i) result += data[i];
  return result;
}

inline uint16_t modbusCrc(const uint8_t *data, size_t size) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
  }
  return crc;
}

inline uint16_t u16le(const uint8_t *p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint16_t u16be(const uint8_t *p) {
  return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}
inline double temperature(uint16_t code) {
  return (code & 0x8000 ? -1.0 : 1.0) * (code & 0x7fff) / 10.0;
}

inline bool decodeBcd(const uint8_t *data, size_t size, bool littleEndian,
                      uint32_t &value) {
  if (!data || !size || size > 4) return false;
  uint32_t result = 0;
  for (size_t i = 0; i < size; ++i) {
    const uint8_t byte = data[littleEndian ? size - 1 - i : i];
    if ((byte >> 4) > 9 || (byte & 15) > 9) return false;
    result = result * 100 + (byte >> 4) * 10 + (byte & 15);
  }
  value = result;
  return true;
}

inline bool isValidId(unsigned id) {
  return id >= 1 && id <= 247 && id != 0xeb;
}

inline unsigned baudRate(uint8_t selector) {
  switch (selector) {
    case 0x96: return 9600;
    case 0x48: return 4800;
    case 0x24: return 2400;
    default: return 1200;
  }
}

inline bool baudSelector(unsigned baud, uint8_t &selector) {
  switch (baud) {
    case 9600: selector = 0x96; return true;
    case 4800: selector = 0x48; return true;
    case 2400: selector = 0x24; return true;
    case 1200: selector = 0x12; return true;
    default: return false;
  }
}

inline Address address(const Identity &identity) {
  return {identity.tuple[3], identity.tuple[2], identity.tuple[1],
          identity.tuple[0], identity.upper[2], identity.upper[1],
          identity.upper[0]};
}

inline bool totalToCoarse(double volumeM3, uint32_t &coarse) {
  if (!std::isfinite(volumeM3) || volumeM3 < 0 ||
      volumeM3 > double(MaxCoarse) / 10000.0)
    return false;
  const double scaled = volumeM3 * 10000.0;
  const double rounded = std::round(scaled);
  // Tolerate floating-point representation error, not fractional steps.
  const double tolerance = 8 * std::numeric_limits<double>::epsilon() *
                           (scaled > 1 ? scaled : 1);
  if (std::abs(scaled - rounded) > tolerance || rounded > MaxCoarse)
    return false;
  coarse = static_cast<uint32_t>(rounded);
  return true;
}

// Exact decimal request input. JSON parsers can narrow numeric literals to
// float, losing more than one 0.1 L step at the high end of the meter range.
inline bool totalToCoarse(const char *text, size_t size, uint32_t &coarse) {
  if (!text || !size) return false;
  uint32_t whole = 0, fraction = 0;
  size_t offset = 0;
  while (offset < size && text[offset] >= '0' && text[offset] <= '9') {
    whole = whole * 10 + uint32_t(text[offset++] - '0');
    if (whole > MaxCoarse / 10000) return false;
  }
  if (!offset) return false;
  if (offset < size) {
    if (text[offset++] != '.') return false;
    const auto digits = size - offset;
    if (!digits || digits > 4) return false;
    while (offset < size) {
      const auto c = text[offset++];
      if (c < '0' || c > '9') return false;
      fraction = fraction * 10 + uint32_t(c - '0');
    }
    for (size_t i = digits; i < 4; ++i) fraction *= 10;
  }
  coarse = whole * 10000 + fraction;
  return true;
}

inline bool leapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

inline bool validRtc(const Rtc &rtc) {
  if (rtc.year < 2000 || rtc.year > 2099 || rtc.month < 1 || rtc.month > 12 ||
      rtc.day < 1 || rtc.hour < 0 || rtc.hour > 23 || rtc.minute < 0 ||
      rtc.minute > 59 || rtc.second < 0 || rtc.second > 59 || rtc.weekday < 0 ||
      rtc.weekday > 6)
    return false;
  constexpr int days[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const int limit = days[rtc.month - 1] +
                    ((rtc.month == 2 && leapYear(rtc.year)) ? 1 : 0);
  return rtc.day <= limit;
}

// Monday=0 is the peer convention qualified on the meter (Thursday=3).
inline int calendarWeekday(int year, int month, int day) {
  constexpr int offsets[]{0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 1 || month > 12) return 0;
  if (month < 3) --year;
  return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] +
          day + 6) % 7;
}

inline Rtc decodeRtc(const uint8_t *raw) {
  Rtc result;
  if (!raw) return result;
  for (size_t i = 0; i < result.raw.size(); ++i) result.raw[i] = raw[i];
  uint32_t fields[6]{};
  bool bcd = true;
  for (size_t i = 0; i < 6; ++i)
    bcd = decodeBcd(raw + i, 1, false, fields[i]) && bcd;
  result.second = fields[0];
  result.minute = fields[1];
  result.hour = fields[2];
  result.day = fields[3];
  result.month = fields[4];
  result.year = 2000 + fields[5];
  result.weekday = raw[6];
  result.valid = bcd && validRtc(result);
  return result;
}

namespace detail {
inline uint8_t bcdByte(unsigned value) {
  return static_cast<uint8_t>((value / 10) * 16 + value % 10);
}
inline void bcd4le(uint32_t value, uint8_t *data) {
  for (size_t i = 0; i < 4; ++i) {
    data[i] = bcdByte(value % 100);
    value /= 100;
  }
}
inline bool replySpec(Command command, uint8_t &reply, size_t &size) {
  switch (command) {
    case Command::HistoryHourly: reply = 0x50; size = 202; break;
    case Command::HistoryDaily: reply = 0x51; size = 124; break;
    case Command::HistoryMonthly: reply = 0x52; size = 96; break;
    case Command::HistoryYearly: reply = 0x53; size = 48; break;
    case Command::SetRtc:
    case Command::SetConfigTotal: reply = 0x9a; size = 0; break;
    case Command::Status: reply = 0x90; size = 18; break;
    case Command::Fine: reply = 0x9b; size = 18; break;
    case Command::Identity: reply = 0x9c; size = 14; break;
    default: return false;
  }
  return true;
}
inline void ebFrame(Command command, const Tuple &tuple, const uint8_t *payload,
                    size_t size, Frame &out) {
  out = {};
  out.bytes[0] = 0xeb;
  out.bytes[1] = static_cast<uint8_t>(command);
  for (size_t i = 0; i < 4; ++i) out.bytes[i + 2] = tuple[i];
  out.bytes[6] = static_cast<uint8_t>(size);
  for (size_t i = 0; i < size; ++i) out.bytes[i + 7] = payload[i];
  out.bytes[size + 7] = sum8(out.bytes.data(), size + 7);
  out.size = size + 8;
}
}  // namespace detail

inline bool buildRead(Command command, const Tuple &tuple, Frame &out) {
  out = {};
  uint8_t reply = 0;
  size_t size = 0;
  if (!detail::replySpec(command, reply, size) || !size) return false;
  detail::ebFrame(command, tuple, nullptr, 0, out);
  return true;
}

inline bool buildSetRtc(const Tuple &tuple, const Rtc &rtc, Frame &out) {
  out = {};
  if (!validRtc(rtc) || rtc.weekday != calendarWeekday(rtc.year, rtc.month, rtc.day))
    return false;
  const uint8_t payload[]{
      detail::bcdByte(rtc.second), detail::bcdByte(rtc.minute),
      detail::bcdByte(rtc.hour), detail::bcdByte(rtc.day),
      detail::bcdByte(rtc.month), detail::bcdByte(rtc.year - 2000),
      static_cast<uint8_t>(rtc.weekday)};
  detail::ebFrame(Command::SetRtc, tuple, payload, sizeof(payload), out);
  return true;
}

// This command deliberately clears the meter's fine remainder/fraction. It is
// also required for ID/baud changes. Callers must obtain a fresh total first.
inline bool buildSetConfigTotal(const Tuple &oldTuple, const Identity &desired,
                                uint32_t coarse, Frame &out) {
  out = {};
  if (!isValidId(desired.tuple[3]) || coarse > MaxCoarse) return false;
  uint8_t payload[11]{};
  for (size_t i = 0; i < 4; ++i) payload[i] = desired.tuple[i];
  detail::bcd4le(coarse, payload + 4);
  for (size_t i = 0; i < 3; ++i) payload[i + 8] = desired.upper[i];
  detail::ebFrame(Command::SetConfigTotal, oldTuple, payload, sizeof(payload), out);
  return true;
}

inline bool buildRtcRead(const Address &target, uint8_t sequence, Frame &out) {
  out = {};
  out.bytes[0] = 0x68;
  out.bytes[1] = 0x10;
  for (size_t i = 0; i < target.size(); ++i) out.bytes[i + 2] = target[i];
  out.bytes[9] = 0x01;
  out.bytes[10] = 3;
  out.bytes[11] = 0x15;
  out.bytes[12] = 0xa0;
  out.bytes[13] = sequence;
  out.bytes[14] = sum8(out.bytes.data(), 14);
  out.bytes[15] = 0x16;
  out.size = 16;
  return true;
}

// Scan complete input, accepting optional preamble/noise and skipping malformed
// candidates. No prefix stripping is performed inside a declared payload.
inline ParseResult findEbReply(const uint8_t *data, size_t size, Command command,
                               const Tuple &expected, EbReply &out,
                               bool discovery = false) {
  uint8_t reply = 0;
  size_t payloadSize = 0;
  if ((!data && size) || !detail::replySpec(command, reply, payloadSize) ||
      (discovery && command != Command::Identity))
    return ParseResult::Invalid;
  bool invalid = false;
  for (size_t start = 0; start < size; ++start) {
    if (data[start] != 0xeb) continue;
    if (size - start < 2) continue;
    if (data[start + 1] != reply) continue;
    if (size - start < 7) continue;
    bool target = true;
    for (size_t i = 0; i < 4; ++i)
      target = target && (discovery || data[start + 2 + i] == expected[i]);
    if (!target || data[start + 6] != payloadSize) {
      invalid = true;
      continue;
    }
    if (size - start < payloadSize + 8) continue;
    if (sum8(data + start, payloadSize + 7) != data[start + payloadSize + 7]) {
      invalid = true;
      continue;
    }
    EbReply result;
    result.request = command;
    for (size_t i = 0; i < 4; ++i) result.tuple[i] = data[start + 2 + i];
    result.size = payloadSize;
    for (size_t i = 0; i < payloadSize; ++i)
      result.payload[i] = data[start + 7 + i];
    out = result;
    return ParseResult::Complete;
  }
  return invalid ? ParseResult::Invalid : ParseResult::Incomplete;
}

inline ParseResult findRtcReply(const uint8_t *data, size_t size,
                                const Address &expected, uint8_t sequence,
                                Rtc &out) {
  if (!data && size) return ParseResult::Invalid;
  bool invalid = false;
  for (size_t start = 0; start < size; ++start) {
    if (data[start] != 0x68 || size - start < 11) continue;
    const uint8_t *p = data + start;
    bool target = true;
    for (size_t i = 0; i < expected.size(); ++i)
      target = target && p[i + 2] == expected[i];
    if (p[1] != 0x10 || !target || p[9] != 0x81 || p[10] != 10) {
      invalid = true;
      continue;
    }
    if (size - start < 23) continue;
    if (p[11] != 0x15 || p[12] != 0xa0 || p[13] != sequence || p[22] != 0x16 ||
        sum8(p, 21) != p[21]) {
      invalid = true;
      continue;
    }
    out = decodeRtc(p + 14);
    return ParseResult::Complete;  // Calendar validity is deliberately separate.
  }
  return invalid ? ParseResult::Invalid : ParseResult::Incomplete;
}

inline bool decodeIdentity(const EbReply &reply, Identity &out) {
  if (reply.request != Command::Identity || reply.size != 14) return false;
  Identity result;
  for (size_t i = 0; i < 5; ++i) result.signature[i] = reply.payload[i];
  result.status = reply.payload[5];
  for (size_t i = 0; i < 4; ++i) result.tuple[i] = reply.payload[i + 6];
  for (size_t i = 0; i < 3; ++i) result.upper[i] = reply.payload[i + 10];
  if (result.tuple != reply.tuple || reply.payload[13] != result.tuple[0])
    return false;
  out = result;
  return true;
}

inline bool decodeFine(const EbReply &reply, Fine &out) {
  if (reply.request != Command::Fine || reply.size != 18) return false;
  Fine result;
  const uint8_t *p = reply.payload.data();
  if (!decodeBcd(p, 4, true, result.coarse) || p[4] > 9) return false;
  result.fraction = u16le(p + 5);
  if (result.fraction >= 16384) return false;
  result.total = result.coarse * 10 + p[4];
  result.volumeM3 = (result.total + result.fraction / 16384.0) / 100000.0;
  result.multiplier = p[7];
  result.captureCount = u16le(p + 8);
  result.voltageMv = u16be(p + 10);
  result.temperatureC = temperature(u16be(p + 12));
  out = result;
  return true;
}

inline bool decodeStatus(const EbReply &reply, Status &out) {
  if (reply.request != Command::Status || reply.size != 18) return false;
  Status result;
  const uint8_t *p = reply.payload.data();
  if (!decodeBcd(p, 4, true, result.coarse)) return false;
  result.intervalMs = u16le(p + 4);
  result.bootCount = u16le(p + 6);
  result.status = p[8];
  result.voltageMv = u16be(p + 11);
  result.temperatureC = temperature(u16be(p + 13));
  out = result;
  return true;
}

inline bool decodeHistory(const EbReply &reply, History &out) {
  History result;
  switch (reply.request) {
    case Command::HistoryHourly: result.count = 48; result.hasCurrent = true; break;
    case Command::HistoryDaily: result.count = 31; break;
    case Command::HistoryMonthly: result.count = 24; break;
    case Command::HistoryYearly: result.count = 12; break;
    default: return false;
  }
  const size_t expected = result.count * 4 + (result.hasCurrent ? 10 : 0);
  if (reply.size != expected) return false;
  for (size_t i = 0; i < result.count; ++i)
    if (!decodeBcd(reply.payload.data() + i * 4, 4, true, result.coarse[i]))
      return false;
  if (result.hasCurrent) {
    if (!decodeBcd(reply.payload.data() + 192, 4, true, result.currentCoarse))
      return false;
    uint8_t raw[7]{};
    for (size_t i = 0; i < 6; ++i) raw[i] = reply.payload[196 + i];
    result.currentRtc = decodeRtc(raw);
    if (result.currentRtc.valid) {
      result.currentRtc.weekday = calendarWeekday(result.currentRtc.year,
          result.currentRtc.month, result.currentRtc.day);
      result.currentRtc.raw[6] = static_cast<uint8_t>(result.currentRtc.weekday);
    }
  }
  out = result;
  return true;
}

inline bool buildFlowRead(uint8_t id, Frame &out) {
  out = {};
  if (!isValidId(id)) return false;
  out.bytes[0] = id;
  out.bytes[1] = 3;
  out.bytes[3] = 2;  // FC03 start 0x0002, count 2.
  out.bytes[5] = 2;
  const uint16_t crc = modbusCrc(out.bytes.data(), 6);
  out.bytes[6] = static_cast<uint8_t>(crc);
  out.bytes[7] = static_cast<uint8_t>(crc >> 8);
  out.size = 8;
  return true;
}

// Flow is the firmware's computed rate, not derived from the fine total delta.
inline ParseResult findFlowReply(const uint8_t *data, size_t size, uint8_t id,
                                 double &litresPerHour) {
  if ((!data && size) || !isValidId(id)) return ParseResult::Invalid;
  bool invalid = false;
  for (size_t start = 0; start < size; ++start) {
    if (data[start] != id || size - start < 3) continue;
    const uint8_t *p = data + start;
    if (p[1] != 3 || p[2] != 4) {
      invalid = true;
      continue;
    }
    if (size - start < 9) continue;
    uint32_t raw = 0;
    if (modbusCrc(p, 7) != u16le(p + 7) || !decodeBcd(p + 3, 4, false, raw)) {
      invalid = true;
      continue;
    }
    // Firmware computes 144 * (0.01 L increments) / (4 Hz ticks): whole L/h.
    litresPerHour = raw;
    return ParseResult::Complete;
  }
  return invalid ? ParseResult::Invalid : ParseResult::Incomplete;
}

}  // namespace esp32m::dev::sm10::protocol
