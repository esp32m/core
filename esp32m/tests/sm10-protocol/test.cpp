#include "esp32m/dev/sm10/protocol.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace esp32m::dev::sm10::protocol;

namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

std::vector<uint8_t> hex(const std::string &text) {
  std::vector<uint8_t> result;
  for (size_t i = 0; i < text.size(); i += 2)
    result.push_back(static_cast<uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
  return result;
}

void equal(const Frame &frame, const std::string &expected) {
  const auto bytes = hex(expected);
  if (frame.size != bytes.size() ||
      !std::equal(bytes.begin(), bytes.end(), frame.bytes.begin()))
    throw std::runtime_error("wire request differs from vector: " + expected);
}

constexpr Tuple tuple{0x96, 0, 0, 1};
constexpr Address target{1, 0, 0, 0x96, 0, 0, 0};

// Fixed vectors from API qualification on 2026-09-24. They are independent
// captured meter replies, not replies generated using the production encoder.
const std::string identityHex = "eb9c960000010ece40250429009600000100000096b9";
const std::string fineHex = "eb9b9600000112730000000681200300000e7200e400000000b0";
const std::string statusHex = "eb90960000011273000000000042040000000e7200e400000041";
const std::string rtcHex = "681001000096000000810a15a00107540000002000cb16";

EbReply reply(Command command, const std::string &text) {
  const auto data = hex(text);
  EbReply result;
  check(findEbReply(data.data(), data.size(), command, tuple, result) ==
            ParseResult::Complete, "captured EB response rejected");
  return result;
}

void capturedTelemetry() {
  Frame frame;
  check(buildRead(Command::Identity, tuple, frame), "build identity read");
  equal(frame, "eb8c96000001000e");
  check(buildRead(Command::Fine, tuple, frame), "build fine read");
  equal(frame, "eb8b96000001000d");
  check(buildRead(Command::Status, tuple, frame), "build status read");
  equal(frame, "eb80960000010002");
  check(buildRtcRead(target, 1, frame), "build A015 read");
  equal(frame, "681001000096000000010315a001c916");

  Identity identity;
  check(decodeIdentity(reply(Command::Identity, identityHex), identity), "identity layout");
  check(identity.tuple == tuple && address(identity) == target &&
            identity.status == 0 && identity.signature[0] == 0xce,
        "identity address reconstruction");
  auto badIdentity = reply(Command::Identity, identityHex);
  badIdentity.payload[13] = 0x48;
  check(!decodeIdentity(badIdentity, identity), "duplicate baud mismatch accepted");
  badIdentity = reply(Command::Identity, identityHex);
  badIdentity.payload[9] = 2;
  check(!decodeIdentity(badIdentity, identity), "inner/outer target mismatch accepted");

  Fine fine;
  check(decodeFine(reply(Command::Fine, fineHex), fine), "fine layout");
  check(fine.coarse == 73 && fine.total == 736 && fine.fraction == 8321 &&
            fine.multiplier == 3 && fine.voltageMv == 3698 &&
            fine.temperatureC == 22.8 &&
            std::abs(fine.volumeM3 - 0.0073650787353515625) < 1e-14,
        "fine total/endianness/units");
  Status status;
  check(decodeStatus(reply(Command::Status, statusHex), status), "status layout");
  check(status.coarse == 73 && status.bootCount == 1090 && status.status == 0 &&
            status.voltageMv == 3698 && status.temperatureC == 22.8,
        "status offsets");
  check(temperature(0x807b) == -12.3 && temperature(0x00e4) == 22.8,
        "temperature sign magnitude");
  auto corruptFine = reply(Command::Fine, fineHex);
  corruptFine.payload[0] = 0xfa;
  check(!decodeFine(corruptFine, fine), "invalid total BCD accepted");
  corruptFine = reply(Command::Fine, fineHex);
  corruptFine.payload[4] = 10;
  check(!decodeFine(corruptFine, fine), "invalid fine remainder accepted");
  corruptFine = reply(Command::Fine, fineHex);
  corruptFine.payload[6] = 0x40;
  check(!decodeFine(corruptFine, fine), "invalid Q14 fraction accepted");
}

void framing() {
  const auto data = hex(fineHex);
  EbReply result;
  for (size_t size = 0; size < data.size(); ++size)
    check(findEbReply(data.data(), size, Command::Fine, tuple, result) ==
              ParseResult::Incomplete, "partial EB frame accepted");
  auto corrupt = data;
  corrupt.back() ^= 1;
  check(findEbReply(corrupt.data(), corrupt.size(), Command::Fine, tuple, result) ==
            ParseResult::Invalid, "bad checksum accepted");
  std::vector<uint8_t> noise{0xfe, 0xfe, 0, 0xeb, 0x9b, 0, 0, 0, 0, 0xff};
  noise.insert(noise.end(), corrupt.begin(), corrupt.end());
  noise.insert(noise.end(), data.begin(), data.end());
  check(findEbReply(noise.data(), noise.size(), Command::Fine, tuple, result) ==
            ParseResult::Complete, "preamble/corrupt candidate recovery");
  check(findEbReply(data.data(), data.size(), Command::Fine, {0x96, 0, 0, 2}, result) ==
            ParseResult::Invalid, "wrong target accepted");
  check(findEbReply(data.data(), data.size(), Command::Fine, tuple, result, true) ==
            ParseResult::Invalid, "discovery permitted for fine command");
  const auto id = hex(identityHex);
  check(findEbReply(id.data(), id.size(), Command::Identity, DiscoveryTuple, result, true) ==
            ParseResult::Complete, "identity discovery actual tuple rejected");
  const auto ack = hex("feeb9a96000001001c");
  check(findEbReply(ack.data(), ack.size(), Command::SetRtc, tuple, result) ==
            ParseResult::Complete && result.size == 0,
        "qualified RTC ACK with external FE preamble");
  corrupt = data;
  corrupt[6] = 0xff;
  check(findEbReply(corrupt.data(), corrupt.size(), Command::Fine, tuple, result) ==
            ParseResult::Invalid, "command-specific length not enforced");
  check(findEbReply(nullptr, 1, Command::Fine, tuple, result) == ParseResult::Invalid,
        "null nonempty input accepted");

  // FE inside a payload is data. Recompute checksum to exercise the envelope,
  // independently of typed BCD field validation.
  corrupt = data;
  corrupt[21] = 0xfe;
  corrupt.back() = sum8(corrupt.data(), corrupt.size() - 1);
  check(findEbReply(corrupt.data(), corrupt.size(), Command::Fine, tuple, result) ==
            ParseResult::Complete && result.payload[14] == 0xfe,
        "payload preamble byte was stripped");
}

void calendar() {
  auto data = hex(rtcHex);
  Rtc rtc;
  check(findRtcReply(data.data(), data.size(), target, 1, rtc) == ParseResult::Complete &&
            !rtc.valid && rtc.month == 0 && rtc.day == 0 && rtc.raw[5] == 0x20,
        "invalid calendar frame must decode for automatic RTC repair");
  for (size_t size = 0; size < data.size(); ++size)
    check(findRtcReply(data.data(), size, target, 1, rtc) == ParseResult::Incomplete,
          "partial RTC frame accepted");
  check(findRtcReply(data.data(), data.size(), target, 2, rtc) == ParseResult::Invalid,
        "wrong RTC sequence accepted");
  auto wrongAddress = target;
  wrongAddress[6] = 1;
  check(findRtcReply(data.data(), data.size(), wrongAddress, 1, rtc) == ParseResult::Invalid,
        "upper address mismatch accepted");
  const auto qualified = hex("681001000096000000810a15a00138011024092603ef16");
  check(findRtcReply(qualified.data(), qualified.size(), target, 1, rtc) ==
            ParseResult::Complete && rtc.valid && rtc.year == 2026 &&
            rtc.month == 9 && rtc.day == 24 && rtc.hour == 10 &&
            rtc.minute == 1 && rtc.second == 38 && rtc.weekday == 3,
        "qualified RTC readback after setting");
  data[21] ^= 1;
  check(findRtcReply(data.data(), data.size(), target, 1, rtc) == ParseResult::Invalid,
        "RTC checksum corruption accepted");
  const uint8_t leap[]{0x59, 0x59, 0x23, 0x29, 0x02, 0x24, 3};
  rtc = decodeRtc(leap);
  check(rtc.valid && rtc.year == 2024 && calendarWeekday(2024, 2, 29) == 3,
        "leap-day decoding");
  Frame frame;
  check(buildSetRtc(tuple, rtc, frame), "leap-day setter");
  check(frame.size == 15 && frame.bytes[1] == 0x44 && frame.bytes[6] == 7,
        "RTC setter exact payload");
  rtc.year = 2023;
  check(!buildSetRtc(tuple, rtc, frame) && frame.size == 0, "invalid leap day write");
  rtc = {2026, 9, 24, 10, 0, 25, 3};
  check(buildSetRtc(tuple, rtc, frame), "qualified RTC date");
  equal(frame, "eb4496000001072500102409260358");
  rtc.weekday = 4;
  check(!buildSetRtc(tuple, rtc, frame), "wrong weekday write");
  rtc.weekday = 3;
  rtc.year = 2100;
  check(!buildSetRtc(tuple, rtc, frame), "unrepresentable century write");
  rtc = {2000, 2, 29, 0, 0, 0, 1};
  check(buildSetRtc(tuple, rtc, frame), "year 2000 is a leap year");
  rtc = {2099, 12, 31, 23, 59, 59, 3};
  check(buildSetRtc(tuple, rtc, frame), "last representable calendar second");
  uint8_t invalidBcd[]{0x6a, 0x00, 0x00, 0x24, 0x09, 0x26, 3};
  check(!decodeRtc(invalidBcd).valid, "invalid RTC BCD accepted");
  invalidBcd[0] = 0;
  invalidBcd[6] = 7;
  check(!decodeRtc(invalidBcd).valid, "invalid weekday accepted");
  invalidBcd[6] = 3;
  invalidBcd[2] = 0x24;
  check(!decodeRtc(invalidBcd).valid, "invalid hour accepted");
}

void configuration() {
  Frame frame;
  for (unsigned command = 0; command < 256; ++command) {
    const bool allowed = command == 0x40 || command == 0x41 || command == 0x42 ||
        command == 0x43 || command == 0x80 || command == 0x8b || command == 0x8c;
    check(buildRead(static_cast<Command>(command), tuple, frame) == allowed,
          "read command allowlist");
    if (!allowed) check(frame.size == 0, "failed builder retained old packet");
  }
  uint8_t selector = 0;
  for (unsigned baud : {1200u, 2400u, 4800u, 9600u}) {
    check(baudSelector(baud, selector) && baudRate(selector) == baud,
          "supported baud mapping");
  }
  check(!baudSelector(19200, selector) && baudRate(0) == 1200, "baud bounds");
  check(!isValidId(0) && !isValidId(235) && !isValidId(248) &&
            isValidId(1) && isValidId(104) && isValidId(247), "ID framing collision");
  uint32_t coarse = 0;
  check(totalToCoarse(0, coarse) && coarse == 0, "zero total");
  check(totalToCoarse(9999.9999, coarse) && coarse == 99999999, "maximum total");
  check(totalToCoarse(12.3456, coarse) && coarse == 123456, "decimal step");
  check(!totalToCoarse(0.00001, coarse) && !totalToCoarse(-1, coarse) &&
            !totalToCoarse(10000, coarse) &&
            !totalToCoarse(std::numeric_limits<double>::infinity(), coarse) &&
            !totalToCoarse(std::numeric_limits<double>::quiet_NaN(), coarse),
        "unrepresentable total accepted");
  Identity desired;
  desired.tuple = {0x48, 0xab, 0xcd, 2};
  desired.upper = {0x11, 0x22, 0x33};
  check(buildSetConfigTotal(tuple, desired, 123456, frame), "build configuration");
  check(frame.size == 19 && frame.bytes[6] == 11 && frame.bytes[2] == 0x96 &&
            frame.bytes[5] == 1 && frame.bytes[7] == 0x48 && frame.bytes[10] == 2 &&
            frame.bytes[11] == 0x56 && frame.bytes[12] == 0x34 &&
            frame.bytes[13] == 0x12 && frame.bytes[14] == 0 &&
            frame.bytes[15] == 0x11 && frame.bytes[16] == 0x22 && frame.bytes[17] == 0x33,
        "coupled write layout/address preservation");
  check(address(desired) == Address{2, 0xcd, 0xab, 0x48, 0x33, 0x22, 0x11},
        "nonzero upper address reconstruction");
  desired.tuple[3] = 235;
  check(!buildSetConfigTotal(tuple, desired, 1, frame) && !frame.size,
        "unsafe ID write accepted");
  desired.tuple[3] = 2;
  check(!buildSetConfigTotal(tuple, desired, 100000000, frame), "BCD overflow accepted");
}

void historiesAndFlow() {
  const std::string daily =
      "eb51960000017c0000000000000000000000000000000000000000000000000000000033000000"
      "33000000380000003900000039000000410000004100000041000000420000004200000042000000"
      "42000000420000004200000042000000420000004200000042000000420000004200000043000000"
      "430000007100000071000000a2";
  History history;
  check(decodeHistory(reply(Command::HistoryDaily, daily), history), "daily bank");
  check(history.count == 31 && !history.hasCurrent && history.coarse[0] == 0 &&
            history.coarse[29] == 71 && history.coarse[30] == 71,
        "oldest-to-newest history order");
  auto malformedHistory = reply(Command::HistoryDaily, daily);
  malformedHistory.payload[120] = 0xff;
  check(!decodeHistory(malformedHistory, history), "history BCD corruption accepted");
  for (const auto command : {Command::HistoryHourly, Command::HistoryDaily,
                            Command::HistoryMonthly, Command::HistoryYearly}) {
    EbReply bank;
    bank.request = command;
    const size_t count = command == Command::HistoryHourly ? 48 :
        command == Command::HistoryDaily ? 31 : command == Command::HistoryMonthly ? 24 : 12;
    bank.size = count * 4 + (command == Command::HistoryHourly ? 10 : 0);
    check(decodeHistory(bank, history) && history.count == count,
          "history response length");
    bank.size--;
    check(!decodeHistory(bank, history), "short history accepted");
  }
  EbReply hourly;
  hourly.request = Command::HistoryHourly;
  hourly.size = 202;
  hourly.payload[192] = 0x73;
  const uint8_t rtcFields[]{0x25, 0x00, 0x10, 0x24, 0x09, 0x26};
  std::copy(std::begin(rtcFields), std::end(rtcFields), hourly.payload.begin() + 196);
  check(decodeHistory(hourly, history) && history.count == 48 && history.hasCurrent &&
            history.currentCoarse == 73 && history.currentRtc.valid &&
            history.currentRtc.weekday == 3 && history.currentRtc.hour == 10,
        "hourly trailer is current calendar, not historical timestamps");
  Frame frame;
  check(buildFlowRead(1, frame), "build flow read");
  equal(frame, "01030002000265cb");
  const auto crcTest = hex("313233343536373839");
  check(modbusCrc(crcTest.data(), crcTest.size()) == 0x4b37, "Modbus CRC reference");
  auto flow = hex("010304000123450000");
  const uint16_t crc = modbusCrc(flow.data(), 7);
  flow[7] = static_cast<uint8_t>(crc);
  flow[8] = static_cast<uint8_t>(crc >> 8);
  double value = 0;
  check(findFlowReply(flow.data(), flow.size(), 1, value) == ParseResult::Complete &&
            value == 12345.0, "flow BCD integer is litres per hour");
  flow[7] ^= 1;
  check(findFlowReply(flow.data(), flow.size(), 1, value) == ParseResult::Invalid,
        "flow CRC rejection");
}

void decimalTotals() {
  struct ValidTotal { const char* text; uint32_t coarse; };
  for (const auto& value : std::vector<ValidTotal>{
           {"0", 0}, {"4", 40000}, {"0.2", 2000}, {"0.2000", 2000},
           {"0.0001", 1}, {"12.3456", 123456}, {"9999.9", 99999000},
           {"9999.9999", 99999999}, {"00000.2000", 2000}}) {
    const std::string text(value.text);
    uint32_t coarse = 0;
    check(totalToCoarse(text.data(), text.size(), coarse) && coarse == value.coarse,
          ("exact decimal total rejected or rounded: " + text).c_str());
  }
  for (const std::string text : {"", ".2", "1.", "0.00001", "0.20000",
           "10000", "9999.99999", "4294967296", "18446744073709551616",
           "9999999999999999999999999999999999999999999999999",
           "+0.2", "-0", "-0.2", " 0.2", "0.2 ", "0.2\n", "0.2x",
           "0,2", "2e-1", "2E-1", "NaN", "Infinity", "0x1", "1..2"}) {
    uint32_t coarse = 0;
    check(!totalToCoarse(text.data(), text.size(), coarse),
          ("invalid decimal total accepted: " + text).c_str());
  }
  uint32_t coarse = 0;
  const char embeddedNull[]{'0', '.', '2', '\0', '5'};
  check(!totalToCoarse(embeddedNull, sizeof(embeddedNull), coarse),
        "embedded NUL discarded trailing decimal bytes");
  check(totalToCoarse(embeddedNull, 3, coarse) && coarse == 2000,
        "decimal parser ignored explicit length");
  const char notTerminated[]{'0', '.', '2'};
  check(totalToCoarse(notTerminated, sizeof(notTerminated), coarse) && coarse == 2000,
        "decimal parser requires a trailing terminator");
  check(!totalToCoarse(nullptr, 0, coarse) && !totalToCoarse(nullptr, 3, coarse),
        "null decimal text accepted");
  // Numeric clients retain strict validation: float narrowing must not silently
  // round 9999.9 to 9999.9004 m3, or turn an off-grid amount into a valid write.
  check(!totalToCoarse(static_cast<double>(static_cast<float>(9999.9)), coarse) &&
            !totalToCoarse(static_cast<double>(static_cast<float>(0.2)), coarse),
        "legacy numeric total validation was loosened after float narrowing");
}
}  // namespace

int main() {
  try {
    capturedTelemetry();
    framing();
    calendar();
    configuration();
    historiesAndFlow();
    decimalTotals();
    std::cout << "SM10 protocol: 6 scenario groups passed\n";
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
