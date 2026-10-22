#include "esp32m/dev/cdhx/protocol.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace p = esp32m::dev::cdhx::protocol;

const p::Frame PublishedRequest = {
    0x78, 0x16, 0x00, 0x16, 0x23, 0x10, 0x37, 0x06,
    0x90, 0x11, 0x94, 0xf0, 0x01, 0x32, 0x08, 0x23,
    0x05, 0x00, 0x01, 0x2c, 0x0d, 0xac, 0xdc, 0xed};
const p::Frame StockRequest = {
    0x76, 0x16, 0x00, 0x00, 0x17, 0x0e, 0x36, 0x05,
    0xdc, 0x10, 0xcc, 0x78, 0x01, 0xcd, 0x08, 0x23,
    0x05, 0x00, 0xeb, 0x01, 0x00, 0xc8, 0x3c, 0x62};
const p::Frame Fixed15 = {
    0x76, 0x16, 0x00, 0x00, 0x0f, 0x0e, 0x36, 0x05,
    0xdc, 0x10, 0xcc, 0x78, 0x01, 0xcd, 0x08, 0x23,
    0x05, 0x00, 0xeb, 0x01, 0x00, 0xc8, 0x9b, 0xe4};
const p::Frame StandbyResponse = {
    0x76, 0x16, 0x00, 0x0a, 0x00, 0x77, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x1a, 0x64, 0x00, 0xc1, 0x8a};

void checksumVectors() {
  const std::string check = "123456789";
  assert(p::crc16(reinterpret_cast<const uint8_t *>(check.data()), check.size()) ==
         0x4b37);
  assert(p::validRequest(PublishedRequest));
  assert(!p::validResponse(PublishedRequest));
  assert(p::validRequest(StockRequest));
  assert(p::validRequest(Fixed15));
  assert(p::validResponse(StandbyResponse));
  // Both directions use 76; validity alone cannot determine direction.
  assert(p::validResponse(StockRequest));
  auto damaged = StockRequest;
  damaged[4] ^= 1;
  assert(!p::validFrame(damaged));
  auto swappedCrc = PublishedRequest;
  swappedCrc[22] = 0xed;
  swappedCrc[23] = 0xdc;
  assert(!p::validFrame(swappedCrc));
  auto special = StockRequest;
  special[0] = 0xff;
  p::updateCrc(special);
  assert(p::validFrame(special));
  assert(!p::validRequest(special));
  assert(!p::validResponse(special));
}

void requestDecoding() {
  p::Request r;
  assert(p::decodeRequest(PublishedRequest, r));
  assert(r.command == 0 && r.ambientTemperatureC == 22);
  assert(r.requestedValueRaw == 35 && r.thermostat() && !r.fixedPower());
  assert(r.minimumPumpDecihz == 16 && r.maximumPumpDecihz == 55);
  assert(r.minimumFanRpm == 1680 && r.maximumFanRpm == 4500);
  assert(r.configuredVoltageDeciv == 240 && r.fanHallPulses == 1);
  assert(r.minimumTemperatureC == 8 && r.maximumTemperatureC == 35);
  assert(r.glowPower == 5 && !r.priming());
  assert(r.unknown18 == 1 && r.unknown19 == 44 && r.altitudeM == 3500);
  assert(r.temperatureWritable());
  assert(p::decodeRequest(Fixed15, r));
  assert(r.requestedValueRaw == 15 && r.fixedPower() && !r.thermostat());
  assert(!r.temperatureWritable());
  auto negative = PublishedRequest;
  negative[3] = 0xfe;
  negative[14] = 0xd8;
  negative[17] = 0x5a;
  p::updateCrc(negative);
  assert(p::decodeRequest(negative, r));
  assert(r.ambientTemperatureC == -2 && r.minimumTemperatureC == -40);
  assert(r.priming() && !r.temperatureWritable());
  auto invalid = negative;
  invalid[22] ^= 1;
  assert(!p::decodeRequest(invalid, r));
  assert(r.ambientTemperatureC == -2);  // Invalid input does not overwrite state.
}

void responseDecoding() {
  p::Response r;
  assert(p::decodeResponse(StandbyResponse, r));
  assert(r.runState == 0 && r.status == 10 && r.supplyVoltageDeciv == 119);
  assert(r.fanRpm == 0 && r.glowCurrentCentia == 3 && r.feedbackRaw == 26);
  assert(r.constant20 == 100 && r.retained17 == 0 && r.auxiliary18 == 0);
  auto running = StandbyResponse;
  running[2] = 5;
  running[3] = 1;
  running[6] = 0x09;
  running[7] = 0xc4;
  running[8] = 0x00;
  running[9] = 120;
  running[10] = 0xff;
  running[11] = 0xd8;
  running[12] = 0x00;
  running[13] = 108;
  running[14] = 0x03;
  running[15] = 0x52;
  running[16] = 35;
  running[17] = 250;
  running[18] = 255;
  p::updateCrc(running);
  assert(p::decodeResponse(running, r));
  assert(r.runState == 5 && r.status == 1 && r.fanRpm == 2500);
  assert(r.fanVoltageDeciv == 120 && r.bodyTemperatureC == -40);
  assert(r.glowVoltageDeciv == 108 && r.glowCurrentCentia == 850);
  assert(r.pumpDecihz == 35 && r.retained17 == 250 && r.auxiliary18 == 255);
  running[10] = 0x80;
  running[11] = 0x00;
  p::updateCrc(running);
  assert(p::decodeResponse(running, r) && r.bodyTemperatureC == -32768);
  assert(!p::decodeResponse(PublishedRequest, r));
  assert(r.bodyTemperatureC == -32768);
}

void commandPatching() {
  for (const auto command : {p::NoCommand, p::StartCommand, p::StopCommand}) {
    auto patched = StockRequest;
    assert(p::patchCommand(patched, command));
    assert(p::validRequest(patched) && patched[2] == command);
    for (std::size_t i = 0; i < 22; ++i)
      if (i != 2) assert(patched[i] == StockRequest[i]);
    if (command == p::StartCommand)
      assert(patched[22] == 0x85 && patched[23] == 0xfa);
    if (command == p::StopCommand)
      assert(patched[22] == 0x69 && patched[23] == 0xae);
  }
  for (const auto command : {0x01, 0x55, 0x5a, 0xff}) {
    auto rejected = StockRequest;
    assert(!p::patchCommand(rejected, command));
    assert(rejected == StockRequest);
  }
  auto damaged = StockRequest;
  damaged[21] ^= 1;
  const auto before = damaged;
  assert(!p::patchCommand(damaged, p::StopCommand) && damaged == before);
  auto special = StockRequest;
  special[0] = 0xff;
  p::updateCrc(special);
  const auto original = special;
  assert(!p::patchCommand(special, p::StartCommand) && special == original);
  // This helper does not queue or retransmit: only caller-owned copy changes.
  assert(StockRequest[2] == 0);
}

void temperaturePatching() {
  for (const int value : {8, 24, 35}) {
    auto patched = PublishedRequest;
    assert(p::patchTemperature(patched, value));
    assert(patched[4] == value && p::validFrame(patched));
    for (std::size_t i = 0; i < 22; ++i)
      if (i != 4) assert(patched[i] == PublishedRequest[i]);
  }
  for (const int value : {-1, 7, 36, 256}) {
    auto rejected = PublishedRequest;
    assert(!p::patchTemperature(rejected, value));
    assert(rejected == PublishedRequest);
  }
  for (const auto mode : {0xcd, 0x00, 0x7f, 0xff}) {
    auto rejected = PublishedRequest;
    rejected[13] = mode;
    p::updateCrc(rejected);
    const auto before = rejected;
    assert(!p::patchTemperature(rejected, 24) && rejected == before);
  }
  for (const auto limits : {std::array<uint8_t, 2>{35, 8}, {8, 8}, {0xd8, 35},
                            {8, 0xff}}) {
    auto rejected = PublishedRequest;
    rejected[14] = limits[0];
    rejected[15] = limits[1];
    p::updateCrc(rejected);
    const auto before = rejected;
    assert(!p::patchTemperature(rejected, 24) && rejected == before);
  }
}

void atomicModePatching() {
  for (const auto& original : {PublishedRequest, StockRequest}) {
    for (const auto target : {p::ThermostatMode, p::FixedPowerMode}) {
      auto patched = original;
      const int value = target == p::ThermostatMode ? 18 : 13;
      assert(p::patchMode(patched, target, value, 22));
      assert(p::validRequest(patched));
      assert(patched[3] == (target == p::ThermostatMode ? 22 : 0));
      assert(patched[4] == value && patched[13] == target);
      for (std::size_t i = 0; i < 22; ++i)
        if (i != 3 && i != 4 && i != 13) assert(patched[i] == original[i]);
    }
  }
  auto measuredZero = PublishedRequest;
  assert(p::patchMode(measuredZero, p::ThermostatMode, 18, 0));
  assert(measuredZero[3] == 0 && measuredZero[13] == p::ThermostatMode);
  auto boundary = PublishedRequest;
  assert(p::patchMode(boundary, p::ThermostatMode, 8, 127));
  assert(p::patchMode(boundary, p::ThermostatMode, 35, 0));
  assert(p::patchMode(boundary, p::FixedPowerMode, 0, 0));
  assert(p::patchMode(boundary, p::FixedPowerMode, 255, 0));

  auto reject = [](const p::Frame& original, uint8_t mode, int value, int ambient) {
    auto frame = original;
    assert(!p::patchMode(frame, mode, value, ambient));
    assert(frame == original);
  };
  reject(PublishedRequest, 0, 18, 22);
  reject(PublishedRequest, 0xff, 18, 22);
  for (const int value : {-1, 7, 36, 256})
    reject(PublishedRequest, p::ThermostatMode, value, 22);
  for (const int ambient : {-128, -1, 128, 256})
    reject(PublishedRequest, p::ThermostatMode, 18, ambient);
  for (const int value : {-1, 256})
    reject(PublishedRequest, p::FixedPowerMode, value, 0);
  for (const auto index : {2, 13, 17, 22}) {
    auto frame = PublishedRequest;
    frame[index] = index == 2 ? p::StartCommand : index == 17 ? 0x5a : 0xff;
    if (index != 22) p::updateCrc(frame);
    reject(frame, p::FixedPowerMode, 13, 0);
  }
  for (const auto limits : {std::array<uint8_t, 2>{35, 8}, {8, 8}, {0xf8, 35},
                            {8, 0xff}}) {
    auto frame = PublishedRequest;
    frame[14] = limits[0];
    frame[15] = limits[1];
    p::updateCrc(frame);
    reject(frame, p::ThermostatMode, 18, 22);
  }
}

void serviceTemplateMatching() {
  for (const auto index : {2, 3, 4, 13}) {
    auto changed = PublishedRequest;
    changed[index] ^= 1;
    p::updateCrc(changed);
    assert(p::sameServiceTemplate(PublishedRequest, changed));
  }
  for (std::size_t index = 0; index < 22; ++index) {
    if (index == 1 || index == 2 || index == 3 || index == 4 || index == 13) continue;
    auto changed = PublishedRequest;
    changed[index] = index == 0 ? 0x76 : static_cast<uint8_t>(changed[index] ^ 1);
    p::updateCrc(changed);
    assert(!p::sameServiceTemplate(PublishedRequest, changed));
  }
  auto damaged = PublishedRequest;
  damaged[22] ^= 1;
  assert(!p::sameServiceTemplate(damaged, PublishedRequest));
  assert(!p::sameServiceTemplate(PublishedRequest, damaged));
}

void serviceParameters() {
  p::ServiceParameters baseline;
  assert(p::decodeService(PublishedRequest, baseline));
  assert(baseline.minimumPumpDecihz == 16 && baseline.maximumPumpDecihz == 55 &&
         baseline.minimumFanRpm == 1680 && baseline.maximumFanRpm == 4500 &&
         baseline.configuredVoltageDeciv == 240 && baseline.fanHallPulses == 1 &&
         baseline.glowPower == 5 && p::validService(baseline));
  for (const int pump : {8, 25}) {
    auto valid = baseline;
    valid.minimumPumpDecihz = pump;
    assert(p::validService(valid));
  }
  for (const int pump : {22, 80}) {
    auto valid = baseline;
    valid.minimumPumpDecihz = 8;
    valid.maximumPumpDecihz = pump;
    assert(p::validService(valid));
  }
  for (const int fan : {1350, 2600}) {
    auto valid = baseline;
    valid.minimumFanRpm = fan;
    assert(p::validService(valid));
  }
  for (const int fan : {3500, 5000}) {
    auto valid = baseline;
    valid.maximumFanRpm = fan;
    assert(p::validService(valid));
  }
  for (const int voltage : {120, 240})
    for (const int hall : {1, 2})
      for (const int glow : {1, 6}) {
        auto selected = baseline;
        selected.configuredVoltageDeciv = voltage;
        selected.fanHallPulses = hall;
        selected.glowPower = glow;
        assert(p::validService(selected));
      }
  auto reject = [&](const p::ServiceParameters& invalid) {
    assert(!p::validService(invalid));
    auto untouched = PublishedRequest;
    assert(!p::patchService(untouched, invalid) && untouched == PublishedRequest);
  };
  for (const int value : {-1, 7, 26, 256}) {
    auto invalid = baseline;
    invalid.minimumPumpDecihz = value;
    reject(invalid);
  }
  for (const int value : {-1, 21, 81, 256}) {
    auto invalid = baseline;
    invalid.maximumPumpDecihz = value;
    reject(invalid);
  }
  auto overlap = baseline;
  overlap.minimumPumpDecihz = 25;
  overlap.maximumPumpDecihz = 22;
  reject(overlap);
  for (const int value : {-1, 1340, 1351, 2599, 2610, 65536}) {
    auto invalid = baseline;
    invalid.minimumFanRpm = value;
    reject(invalid);
  }
  for (const int value : {-1, 3490, 3501, 4999, 5010, 65536}) {
    auto invalid = baseline;
    invalid.maximumFanRpm = value;
    reject(invalid);
  }
  for (const int value : {-1, 0, 119, 121, 239, 241, 256}) {
    auto invalid = baseline;
    invalid.configuredVoltageDeciv = value;
    reject(invalid);
  }
  for (const int value : {-1, 0, 3, 256}) {
    auto invalid = baseline;
    invalid.fanHallPulses = value;
    reject(invalid);
  }
  for (const int value : {-1, 0, 7, 256}) {
    auto invalid = baseline;
    invalid.glowPower = value;
    reject(invalid);
  }

  auto selected = baseline;
  selected.minimumPumpDecihz = 8;
  selected.maximumPumpDecihz = 80;
  selected.minimumFanRpm = 1350;
  selected.maximumFanRpm = 5000;
  selected.configuredVoltageDeciv = 120;
  selected.fanHallPulses = 2;
  selected.glowPower = 6;
  for (const int header : {0x76, 0x78})
    for (const int command : {0, 0xa0, 0x05, 0xff})
      for (const int prime : {0, 0x5a, 0xff}) {
        auto original = PublishedRequest;
        original[0] = header;
        original[2] = command;
        original[17] = prime;
        p::updateCrc(original);
        auto patched = original;
        assert(p::patchService(patched, selected) && p::validRequest(patched));
        p::ServiceParameters decoded;
        assert(p::decodeService(patched, decoded));
        assert(decoded.minimumPumpDecihz == 8 && decoded.maximumPumpDecihz == 80 &&
               decoded.minimumFanRpm == 1350 && decoded.maximumFanRpm == 5000 &&
               decoded.configuredVoltageDeciv == 120 && decoded.fanHallPulses == 2 &&
               decoded.glowPower == 6);
        for (std::size_t i = 0; i < 22; ++i)
          if ((i < 5 || i > 12) && i != 16) assert(patched[i] == original[i]);
      }
  for (const int index : {0, 22}) {
    auto invalidFrame = PublishedRequest;
    invalidFrame[index] = index == 0 ? 0xff : 0;
    if (index == 0) p::updateCrc(invalidFrame);
    const auto unchanged = invalidFrame;
    assert(!p::patchService(invalidFrame, selected) && invalidFrame == unchanged);
    auto decoded = baseline;
    assert(!p::decodeService(invalidFrame, decoded));
    assert(decoded.minimumPumpDecihz == baseline.minimumPumpDecihz);
  }
}

void persistentOperatingPatching() {
  for (const int header : {0x76, 0x78})
    for (const int command : {0, 0xa0, 0x05, 0xff})
      for (const int prime : {0, 0x5a, 0xff}) {
        auto original = PublishedRequest;
        original[0] = header;
        original[2] = command;
        original[17] = prime;
        p::updateCrc(original);
        for (const auto mode : {p::ThermostatMode, p::FixedPowerMode}) {
          auto patched = original;
          assert(p::patchOperating(patched, mode, mode == p::ThermostatMode ? 18 : 13, 22));
          assert(p::validRequest(patched));
          assert(patched[3] == (mode == p::ThermostatMode ? 22 : 0));
          assert(patched[4] == (mode == p::ThermostatMode ? 18 : 13) && patched[13] == mode);
          for (std::size_t i = 0; i < 22; ++i)
            if (i != 3 && i != 4 && i != 13) assert(patched[i] == original[i]);
        }
      }
  auto reject = [](const p::Frame& original, uint8_t mode, int value, int ambient) {
    auto frame = original;
    assert(!p::patchOperating(frame, mode, value, ambient) && frame == original);
  };
  for (const int value : {-1, 7, 36, 256})
    reject(PublishedRequest, p::ThermostatMode, value, 22);
  for (const int ambient : {-1, 128, 256})
    reject(PublishedRequest, p::ThermostatMode, 18, ambient);
  reject(PublishedRequest, 0, 18, 22);
  reject(PublishedRequest, p::FixedPowerMode, -1, 0);
  reject(PublishedRequest, p::FixedPowerMode, 256, 0);
  auto damaged = PublishedRequest;
  damaged[22] ^= 1;
  reject(damaged, p::ThermostatMode, 18, 22);
}

void parserRecovery() {
  p::Parser parser;
  p::Frame frame{};
  uint64_t now = 100000;
  auto feed = [&](uint8_t byte) {
    now += p::CharacterTimeUs;
    return parser.feed(byte, now, frame);
  };
  // False headers, invalid length, and a truncated candidate before real data.
  for (uint8_t byte : {0x00, 0x76, 0x76, 0x16, 0x01, 0x78, 0x03})
    assert(!feed(byte));
  unsigned emitted = 0;
  for (uint8_t byte : PublishedRequest) emitted += feed(byte);
  assert(emitted == 1 && frame == PublishedRequest);
  assert(parser.buffered() == 0 && parser.stats().crcErrors > 0);
  assert(parser.stats().lengthErrors > 0 && parser.stats().noiseBytes > 0);
  for (uint8_t byte : StandbyResponse) emitted += feed(byte);
  assert(emitted == 2 && frame == StandbyResponse);
  auto damaged = StockRequest;
  damaged[15] ^= 1;
  for (uint8_t byte : damaged) assert(!feed(byte));
  for (uint8_t byte : Fixed15) emitted += feed(byte);
  assert(emitted == 3 && frame == Fixed15);
  assert(parser.stats().frames == 3);
}

void parserTimeouts() {
  p::Parser parser;
  p::Frame frame{};
  for (unsigned i = 0; i < 8; ++i)
    assert(!parser.feed(StockRequest[i], i * 400 + 1000, frame));
  assert(parser.buffered() == 8);
  assert(!parser.expire(1000 + 7 * 400 + p::FrameGapUs - 1));
  assert(parser.expire(1000 + 7 * 400 + p::FrameGapUs));
  assert(parser.buffered() == 0 && parser.stats().truncated == 1);
  // A new frame after a gap is not combined with stale bytes.
  assert(!parser.feed(0x76, 100000, frame));
  unsigned emitted = 0;
  for (std::size_t i = 0; i < StockRequest.size(); ++i)
    emitted += parser.feed(StockRequest[i], 200000 + i * 400, frame);
  assert(emitted == 1 && frame == StockRequest && parser.stats().truncated == 2);
  assert(!parser.feed(0x78, 300000, frame));
  assert(parser.expire(100));  // A monotonic-clock reset invalidates a partial frame.
  assert(parser.stats().truncated == 3);
  parser.reset();
  assert(parser.stats().frames == 1);
}

int main() {
  checksumVectors();
  requestDecoding();
  responseDecoding();
  commandPatching();
  temperaturePatching();
  atomicModePatching();
  serviceTemplateMatching();
  serviceParameters();
  persistentOperatingPatching();
  parserRecovery();
  parserTimeouts();
  std::cout << "cdhx-protocol: 11 scenario groups passed\n";
}
