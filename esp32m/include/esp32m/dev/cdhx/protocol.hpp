#pragma once

// Normal CDHX1/H45214A0 display protocol. This helper has no ESP-IDF dependency.
// Requests carry persistent heater tuning: patch captured frames, never invent
// defaults. Direction is supplied by the physical endpoint, not header 0x76.
#include <array>
#include <cstddef>
#include <cstdint>

namespace esp32m::dev::cdhx::protocol {

constexpr std::size_t FrameSize = 24;
constexpr uint32_t BaudRate = 25000;
constexpr uint64_t CharacterTimeUs = 400;
constexpr uint64_t FrameGapUs = 20000;
constexpr uint8_t ThermostatMode = 0x32;
constexpr uint8_t FixedPowerMode = 0xcd;
constexpr uint8_t NoCommand = 0x00;
constexpr uint8_t StartCommand = 0xa0;
constexpr uint8_t StopCommand = 0x05;
using Frame = std::array<uint8_t, FrameSize>;

inline uint16_t crc16(const uint8_t *data, std::size_t size) {
  uint16_t crc = 0xffff;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = static_cast<uint16_t>((crc >> 1) ^ ((crc & 1) ? 0xa001 : 0));
  }
  return crc;
}

inline uint16_t u16be(const uint8_t *data) {
  return static_cast<uint16_t>((uint16_t(data[0]) << 8) | data[1]);
}

inline int16_t s8(uint8_t value) {
  return static_cast<int16_t>(value < 128 ? value : int(value) - 256);
}

inline int16_t s16be(const uint8_t *data) {
  const uint16_t value = u16be(data);
  return static_cast<int16_t>(value < 0x8000 ? int32_t(value)
                                           : int32_t(value) - 0x10000);
}

inline bool isHeader(uint8_t value) {
  return value == 0x76 || value == 0x78 || value == 0xff;
}

inline void updateCrc(Frame &frame) {
  const uint16_t crc = crc16(frame.data(), FrameSize - 2);
  frame[22] = static_cast<uint8_t>(crc >> 8);
  frame[23] = static_cast<uint8_t>(crc);
}

// Special FF frames remain raw-only. Non-normal packet lengths are unsupported.
inline bool validFrame(const Frame &frame) {
  return isHeader(frame[0]) && frame[1] == 0x16 &&
         crc16(frame.data(), FrameSize - 2) == u16be(frame.data() + 22);
}

inline bool validRequest(const Frame &frame) {
  return (frame[0] == 0x76 || frame[0] == 0x78) && validFrame(frame);
}

inline bool validResponse(const Frame &frame) {
  return frame[0] == 0x76 && validFrame(frame);
}

struct Request {
  uint8_t command = 0;
  int16_t ambientTemperatureC = 0;
  uint8_t requestedValueRaw = 0;
  uint8_t minimumPumpDecihz = 0, maximumPumpDecihz = 0;
  uint16_t minimumFanRpm = 0, maximumFanRpm = 0;
  uint8_t configuredVoltageDeciv = 0, fanHallPulses = 0, mode = 0;
  // Exact H452 firmware interprets configuration limits as signed bytes.
  int16_t minimumTemperatureC = 0, maximumTemperatureC = 0;
  uint8_t glowPower = 0, primeByte = 0, unknown18 = 0, unknown19 = 0;
  uint16_t altitudeM = 0;
  bool thermostat() const { return mode == ThermostatMode; }
  bool fixedPower() const { return mode == FixedPowerMode; }
  bool priming() const { return primeByte == 0x5a; }
  bool temperatureWritable() const {
    return thermostat() && minimumTemperatureC >= 0 &&
           minimumTemperatureC < maximumTemperatureC;
  }
};

struct Response {
  uint8_t runState = 0, status = 0;
  uint16_t supplyVoltageDeciv = 0, fanRpm = 0, fanVoltageDeciv = 0;
  int16_t bodyTemperatureC = 0;
  uint16_t glowVoltageDeciv = 0, glowCurrentCentia = 0;
  // Enabled base setting, not independently measured pump motion/fuel flow.
  uint8_t pumpDecihz = 0;
  // Byte 17 is retained configuration, not a demonstrated runtime fault latch.
  uint8_t retained17 = 0, auxiliary18 = 0, feedbackRaw = 0;
  // H452 serializes literal 100 at 20; do not present it as temperature.
  uint8_t constant20 = 0, reserved21 = 0;
};

// Display-observed editing limits; these do not qualify every operating
// combination. Integers keep the persisted/wire representation exact.
struct ServiceParameters {
  int minimumPumpDecihz = 0, maximumPumpDecihz = 0;
  int minimumFanRpm = 0, maximumFanRpm = 0;
  int configuredVoltageDeciv = 0, fanHallPulses = 0, glowPower = 0;
  bool operator==(const ServiceParameters& other) const {
    return minimumPumpDecihz == other.minimumPumpDecihz &&
        maximumPumpDecihz == other.maximumPumpDecihz &&
        minimumFanRpm == other.minimumFanRpm && maximumFanRpm == other.maximumFanRpm &&
        configuredVoltageDeciv == other.configuredVoltageDeciv &&
        fanHallPulses == other.fanHallPulses && glowPower == other.glowPower;
  }
};

inline bool validService(const ServiceParameters& value) {
  return value.minimumPumpDecihz >= 8 && value.minimumPumpDecihz <= 25 &&
      value.maximumPumpDecihz >= 22 && value.maximumPumpDecihz <= 80 &&
      value.minimumPumpDecihz <= value.maximumPumpDecihz &&
      value.minimumFanRpm >= 1350 && value.minimumFanRpm <= 2600 &&
      value.minimumFanRpm % 10 == 0 &&
      value.maximumFanRpm >= 3500 && value.maximumFanRpm <= 5000 &&
      value.maximumFanRpm % 10 == 0 &&
      (value.configuredVoltageDeciv == 120 || value.configuredVoltageDeciv == 240) &&
      (value.fanHallPulses == 1 || value.fanHallPulses == 2) &&
      value.glowPower >= 1 && value.glowPower <= 6;
}

inline ServiceParameters serviceParameters(const Request& request) {
  return {request.minimumPumpDecihz, request.maximumPumpDecihz,
      request.minimumFanRpm, request.maximumFanRpm, request.configuredVoltageDeciv,
      request.fanHallPulses, request.glowPower};
}

inline bool decodeRequest(const Frame &frame, Request &request) {
  if (!validRequest(frame)) return false;
  Request result;
  result.command = frame[2];
  result.ambientTemperatureC = s8(frame[3]);
  result.requestedValueRaw = frame[4];
  result.minimumPumpDecihz = frame[5];
  result.maximumPumpDecihz = frame[6];
  result.minimumFanRpm = u16be(frame.data() + 7);
  result.maximumFanRpm = u16be(frame.data() + 9);
  result.configuredVoltageDeciv = frame[11];
  result.fanHallPulses = frame[12];
  result.mode = frame[13];
  result.minimumTemperatureC = s8(frame[14]);
  result.maximumTemperatureC = s8(frame[15]);
  result.glowPower = frame[16];
  result.primeByte = frame[17];
  result.unknown18 = frame[18];
  result.unknown19 = frame[19];
  result.altitudeM = u16be(frame.data() + 20);
  request = result;
  return true;
}

inline bool decodeResponse(const Frame &frame, Response &response) {
  if (!validResponse(frame)) return false;
  Response result;
  result.runState = frame[2];
  result.status = frame[3];
  result.supplyVoltageDeciv = u16be(frame.data() + 4);
  result.fanRpm = u16be(frame.data() + 6);
  result.fanVoltageDeciv = u16be(frame.data() + 8);
  result.bodyTemperatureC = s16be(frame.data() + 10);
  result.glowVoltageDeciv = u16be(frame.data() + 12);
  result.glowCurrentCentia = u16be(frame.data() + 14);
  result.pumpDecihz = frame[16];
  result.retained17 = frame[17];
  result.auxiliary18 = frame[18];
  result.feedbackRaw = frame[19];
  result.constant20 = frame[20];
  result.reserved21 = frame[21];
  response = result;
  return true;
}

inline bool decodeService(const Frame& frame, ServiceParameters& service) {
  Request request;
  if (!decodeRequest(frame, request)) return false;
  const auto result = serviceParameters(request);
  if (!validService(result)) return false;
  service = result;
  return true;
}

inline bool patchService(Frame& frame, const ServiceParameters& service) {
  if (!validRequest(frame) || !validService(service)) return false;
  frame[5] = static_cast<uint8_t>(service.minimumPumpDecihz);
  frame[6] = static_cast<uint8_t>(service.maximumPumpDecihz);
  frame[7] = static_cast<uint8_t>(service.minimumFanRpm >> 8);
  frame[8] = static_cast<uint8_t>(service.minimumFanRpm);
  frame[9] = static_cast<uint8_t>(service.maximumFanRpm >> 8);
  frame[10] = static_cast<uint8_t>(service.maximumFanRpm);
  frame[11] = static_cast<uint8_t>(service.configuredVoltageDeciv);
  frame[12] = static_cast<uint8_t>(service.fanHallPulses);
  frame[16] = static_cast<uint8_t>(service.glowPower);
  updateCrc(frame);
  return true;
}

inline bool patchCommand(Frame &frame, uint8_t command) {
  if (!validRequest(frame) ||
      (command != NoCommand && command != StartCommand && command != StopCommand))
    return false;
  frame[2] = command;
  updateCrc(frame);
  return true;
}

inline bool patchTemperature(Frame &frame, int temperatureC) {
  Request request;
  if (!decodeRequest(frame, request) || !request.temperatureWritable() ||
      temperatureC < request.minimumTemperatureC ||
      temperatureC > request.maximumTemperatureC)
    return false;
  frame[4] = static_cast<uint8_t>(temperatureC);
  updateCrc(frame);
  return true;
}

// The UART owner qualifies the target power range and ambient source. The
// physical display's fixed-power ambient byte is a placeholder, not a sensor.
// Validate the complete selection before replacing any caller-owned bytes.
inline bool patchMode(Frame &frame, uint8_t mode, int value, int ambientC) {
  Request request;
  if (!decodeRequest(frame, request) ||
      (!request.thermostat() && !request.fixedPower()) ||
      request.command != NoCommand || request.primeByte != 0)
    return false;
  if (mode == ThermostatMode) {
    if (request.minimumTemperatureC < 0 ||
        request.minimumTemperatureC >= request.maximumTemperatureC ||
        value < request.minimumTemperatureC ||
        value > request.maximumTemperatureC || ambientC < 0 || ambientC > 127)
      return false;
  } else if (mode == FixedPowerMode) {
    if (value < 0 || value > 255) return false;
  } else
    return false;
  Frame result = frame;
  result[3] = mode == ThermostatMode ? static_cast<uint8_t>(ambientC) : 0;
  result[4] = static_cast<uint8_t>(value);
  result[13] = mode;
  updateCrc(result);
  frame = result;
  return true;
}

// Persistent production settings accompany physical Start/Stop/prime events.
// Do not erase those events when substituting mode/value. A power-mode ambient
// placeholder is never a usable thermostat observation.
inline bool patchOperating(Frame& frame, uint8_t mode, int value, int ambientC) {
  Request request;
  if (!decodeRequest(frame, request) || (!request.thermostat() && !request.fixedPower()))
    return false;
  if (mode == ThermostatMode) {
    if (!request.thermostat() || request.minimumTemperatureC < 0 ||
        request.minimumTemperatureC >= request.maximumTemperatureC ||
        value < request.minimumTemperatureC || value > request.maximumTemperatureC ||
        ambientC < 0 || ambientC > 127) return false;
  } else if (mode == FixedPowerMode) {
    if (value < 0 || value > 255) return false;
  } else return false;
  frame[3] = mode == ThermostatMode ? static_cast<uint8_t>(ambientC) : 0;
  frame[4] = static_cast<uint8_t>(value);
  frame[13] = mode;
  updateCrc(frame);
  return true;
}

// Captured tuning and opaque fields identify a service template. Commands are
// intentionally excluded from identity; action eligibility checks them apart.
inline bool sameServiceTemplate(const Frame &first, const Frame &second) {
  if (!validRequest(first) || !validRequest(second)) return false;
  for (std::size_t i = 0; i < FrameSize - 2; ++i) {
    if (i == 2 || i == 3 || i == 4 || i == 13) continue;
    if (first[i] != second[i]) return false;
  }
  return true;
}

// Fixed-size streaming parser. A failed candidate slides by one byte so a
// following valid frame is recoverable even when noise contains false headers.
// Timestamps are monotonic observations supplied by the UART owner. Counters
// survive reset(), allowing the driver to report faults after clearing buffers.
class Parser {
 public:
  struct Stats {
    uint64_t frames = 0, crcErrors = 0, lengthErrors = 0;
    uint64_t truncated = 0, noiseBytes = 0;
  };

  // A UART batch usually supplies service times rather than byte arrival
  // times. Such callers drain ordered bytes with checkGap=false, then call
  // expire() when no further data is available. CRC/length checks still apply.
  bool feed(uint8_t byte, uint64_t nowUs, Frame &completed, bool checkGap = true) {
    if (checkGap) expire(nowUs);
    bytes_[used_++] = byte;
    lastUs_ = nowUs;
    while (used_) {
      if (!isHeader(bytes_[0])) {
        ++stats_.noiseBytes;
        discard(1);
        continue;
      }
      if (used_ < 2) return false;
      if (bytes_[1] != 0x16) {
        ++stats_.lengthErrors;
        discard(1);
        continue;
      }
      if (used_ < FrameSize) return false;
      if (validFrame(bytes_)) {
        completed = bytes_;
        used_ = 0;
        ++stats_.frames;
        return true;
      }
      ++stats_.crcErrors;
      discard(1);
    }
    return false;
  }

  bool expire(uint64_t nowUs, uint64_t gapUs = FrameGapUs) {
    if (!used_ || (nowUs >= lastUs_ && nowUs - lastUs_ < gapUs)) return false;
    ++stats_.truncated;
    reset();
    return true;
  }

  void reset() { used_ = 0; }
  std::size_t buffered() const { return used_; }
  const Stats &stats() const { return stats_; }

 private:
  void discard(std::size_t count) {
    used_ -= count;
    for (std::size_t i = 0; i < used_; ++i) bytes_[i] = bytes_[i + count];
  }
  Frame bytes_{};
  std::size_t used_ = 0;
  uint64_t lastUs_ = 0;
  Stats stats_{};
};

}  // namespace esp32m::dev::cdhx::protocol
