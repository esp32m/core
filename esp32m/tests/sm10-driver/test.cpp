#include <ArduinoJson.h>
#include "esp32m/dev/sm10/protocol.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = 0x102,
    ESP_ERR_INVALID_STATE = 0x103, ESP_ERR_INVALID_SIZE = 0x104, ESP_ERR_TIMEOUT = 0x107,
    ESP_ERR_INVALID_RESPONSE = 0x108;
#define ESP_CHECK_RETURN(x) do { const auto error = (x); if (error != ESP_OK) return error; } while (0)

const char* esp_err_to_name(esp_err_t error) {
  switch (error) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_RESPONSE: return "ESP_ERR_INVALID_RESPONSE";
    default: return "ESP_FAIL";
  }
}

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

namespace fake {
uint64_t nowMs = 1000;
time_t wallBase = 0;
unsigned persisted = 0;
time_t epoch(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) {
  tm value{};
  value.tm_year = year - 1900;
  value.tm_mon = month - 1;
  value.tm_mday = day;
  value.tm_hour = hour;
  value.tm_min = minute;
  value.tm_sec = second;
#ifdef _WIN32
  return _mkgmtime(&value);
#else
  return timegm(&value);
#endif
}
}  // namespace fake

int64_t esp_timer_get_time() { return static_cast<int64_t>(fake::nowMs * 1000); }
time_t fake_time(time_t* result) {
  const auto value = fake::wallBase + static_cast<time_t>(fake::nowMs / 1000);
  if (result) *result = value;
  return value;
}
tm* fake_localtime_r(const time_t* value, tm* result) {
  const auto* converted = std::gmtime(value);
  if (!converted) return nullptr;
  *result = *converted;
  return result;
}
time_t fake_mktime(tm* value) {
  return fake::epoch(value->tm_year + 1900, value->tm_mon + 1, value->tm_mday,
                     value->tm_hour, value->tm_min, value->tm_sec);
}

namespace esp32m {
struct Request {
  std::string command;
  JsonDocument payload;
  JsonDocument response;
  bool internal = false;
  bool broadcast = false;
  bool responded = false;
  esp_err_t error = ESP_OK;
  explicit Request(const char* cmd = "", bool internal = false)
      : command(cmd), internal(internal) {}
  bool is(const char* value) const { return command == value; }
  bool isInternal() const { return internal; }
  bool isBroadcast() const { return broadcast; }
  JsonVariantConst data() const { return payload.as<JsonVariantConst>(); }
  void respondError(esp_err_t value, const char*) { error = value; responded = true; }
  void respond(esp_err_t value) { error = value; responded = true; }
  void respond(JsonVariantConst value, bool) {
    response.set(value);
    error = ESP_OK;
    responded = true;
  }
};
struct ErrorList {
  esp_err_t error = ESP_OK;
  void check(esp_err_t value, const char* = nullptr) { error = value; }
};
struct RequestContext {
  Request& request;
  JsonVariantConst data;
  ErrorList errors;
  explicit RequestContext(Request& request) : request(request), data(request.data()) {}
};
struct AppObject {
  virtual ~AppObject() = default;
  virtual const char* name() const { return "fake"; }
  virtual bool handleRequest(Request&) { return false; }
};
struct Device : AppObject {
  enum Flags { HasSensors=1, CanDisable=2 };
  void setSensorsPollInterval(unsigned) {}
  void init(int) {}
  bool isDisabled() const { return false; }
  std::recursive_mutex _deviceMutex;
  virtual bool initSensors() { return false; }
  virtual bool pollSensors() { return false; }
  virtual JsonDocument* getState(RequestContext&) { return nullptr; }
  virtual JsonDocument* getDeviceConfig(RequestContext&) { return nullptr; }
  virtual bool setDeviceConfig(RequestContext&) { return false; }
};
enum class StateClass { Measurement, Total };
struct Sensor {
  const char* unit = nullptr;
  unsigned precision = 0;
  StateClass stateClass = StateClass::Measurement;
  double value = 0;
  bool valid = false;
  Sensor(Device*, const char*, const char*) {}
  void setTitle(const char*) {}
  void set(double next) { value = next; valid = true; }
  void resetState() { valid = false; }
};
namespace config {
struct Changed { static void publish(void*) { ++fake::persisted; } };
}
}  // namespace esp32m

namespace fake {
namespace p = esp32m::dev::sm10::protocol;
struct Transaction {
  std::vector<uint8_t> bytes;
  uint32_t baud;
  bool fullCapture = false;
};
struct Meter {
  p::Identity identity{};
  uint32_t baud = 9600, total = 736, flow = 0;
  uint16_t fraction = 8321, bootCount = 1090;
  bool rtcValid = false, failRtcRead = false, failFineRead = false;
  bool dropAck = false, offlineAfterWrite = false, offline = false;
  bool ignoreRtcWrite = false, ignoreConfigWrite = false;
  bool failUnicastIdentity = false, overrideDiscovery = false;
  bool overrideUnicastIdentity = false, overrideFine = false;
  std::vector<uint8_t> discoveryReply, unicastIdentityReply, fineReply;
  esp_err_t discoveryTransportError = ESP_OK;
  time_t rtcEpoch = 0;
  uint64_t rtcSetMs = 0;
  std::vector<Transaction> transactions;
  Meter() {
    identity.tuple = {0x96, 0, 0, 1};
    identity.signature = {0xce, 0x40, 0x25, 0x04, 0x29};
  }
  void setValidRtc() {
    rtcValid = true;
    rtcEpoch = fake_time(nullptr);
    rtcSetMs = nowMs;
  }
  unsigned count(uint8_t command) const {
    return static_cast<unsigned>(std::count_if(transactions.begin(), transactions.end(),
        [command](const auto& t) { return t.bytes[0] == 0xeb && t.bytes[1] == command; }));
  }
  unsigned writes() const { return count(0x44) + count(0x85); }
  static bool isDiscovery(const std::vector<uint8_t>& request) {
    return request.size() == 8 && request[0] == 0xeb && request[1] == 0x8c &&
        std::equal(p::DiscoveryTuple.begin(), p::DiscoveryTuple.end(),
                   request.begin() + 2);
  }
  unsigned discoveries() const {
    return static_cast<unsigned>(std::count_if(transactions.begin(), transactions.end(),
        [](const auto& transaction) { return isDiscovery(transaction.bytes); }));
  }
  unsigned rtcReads() const {
    return static_cast<unsigned>(std::count_if(transactions.begin(), transactions.end(),
        [](const auto& t) { return t.bytes[0] == 0x68; }));
  }
  std::array<uint8_t, 7> rtcBytes() const {
    if (!rtcValid) return {7, 0x54, 0, 0, 0, 0x20, 0};
    const auto value = rtcEpoch + static_cast<time_t>((nowMs - rtcSetMs) / 1000);
    tm calendar{};
    fake_localtime_r(&value, &calendar);
    const auto bcd = [](int value) { return uint8_t((value / 10) * 16 + value % 10); };
    return {bcd(calendar.tm_sec), bcd(calendar.tm_min), bcd(calendar.tm_hour),
            bcd(calendar.tm_mday), bcd(calendar.tm_mon + 1), bcd(calendar.tm_year - 100),
            uint8_t((calendar.tm_wday + 6) % 7)};
  }
  static void bcd4(uint32_t value, uint8_t* target, bool little = true) {
    for (size_t i = 0; i < 4; ++i) {
      target[little ? i : 3 - i] = uint8_t((value % 100 / 10) * 16 + value % 10);
      value /= 100;
    }
  }
  std::vector<uint8_t> eb(uint8_t command, const std::vector<uint8_t>& payload) const {
    std::vector<uint8_t> reply{0xfe, 0xeb, command};
    reply.insert(reply.end(), identity.tuple.begin(), identity.tuple.end());
    reply.push_back(uint8_t(payload.size()));
    reply.insert(reply.end(), payload.begin(), payload.end());
    reply.push_back(p::sum8(reply.data() + 1, reply.size() - 1));
    return reply;
  }
  std::vector<uint8_t> identityReply() const {
    std::vector<uint8_t> payload(identity.signature.begin(), identity.signature.end());
    payload.push_back(0);
    payload.insert(payload.end(), identity.tuple.begin(), identity.tuple.end());
    payload.insert(payload.end(), identity.upper.begin(), identity.upper.end());
    payload.push_back(identity.tuple[0]);
    return eb(0x9c, payload);
  }
  std::vector<uint8_t> respond(const std::vector<uint8_t>& request, uint32_t speed) {
    transactions.push_back({request, speed});
    nowMs += 200;
    if (offline || speed != baud) return {};
    if (request[0] == 0xeb) {
      const auto discovery = isDiscovery(request);
      if (!discovery &&
          !std::equal(identity.tuple.begin(), identity.tuple.end(), request.begin() + 2))
        return {};
      check(request.size() == size_t(request[6]) + 8 &&
                p::sum8(request.data(), request.size() - 1) == request.back(),
            "driver transmitted invalid EB envelope");
      const auto command = request[1];
      if (command == 0x8c) {
        if (discovery && overrideDiscovery) return discoveryReply;
        if (!discovery && failUnicastIdentity) return {};
        if (!discovery && overrideUnicastIdentity) return unicastIdentityReply;
        return identityReply();
      }
      if (command == 0x8b) {
        if (failFineRead) return {};
        if (overrideFine) return fineReply;
        std::vector<uint8_t> payload(18);
        bcd4(total / 10, payload.data());
        payload[4] = total % 10;
        payload[5] = uint8_t(fraction);
        payload[6] = uint8_t(fraction >> 8);
        payload[7] = 3;
        payload[10] = 0x0e;
        payload[11] = 0x72;
        payload[13] = 0xe4;
        return eb(0x9b, payload);
      }
      if (command == 0x80) {
        std::vector<uint8_t> payload(18);
        bcd4(total / 10, payload.data());
        payload[6] = uint8_t(bootCount);
        payload[7] = uint8_t(bootCount >> 8);
        payload[11] = 0x0e;
        payload[12] = 0x72;
        payload[14] = 0xe4;
        return eb(0x90, payload);
      }
      if (command >= 0x40 && command <= 0x43) {
        const size_t count = command == 0x40 ? 48 : command == 0x41 ? 31 :
            command == 0x42 ? 24 : 12;
        std::vector<uint8_t> payload(count * 4 + (command == 0x40 ? 10 : 0));
        for (size_t i = 0; i < count; ++i) bcd4(uint32_t(i), payload.data() + 4 * i);
        if (command == 0x40) {
          bcd4(total / 10, payload.data() + 192);
          const auto raw = rtcBytes();
          std::copy(raw.begin(), raw.begin() + 6, payload.begin() + 196);
        }
        return eb(command + 0x10, payload);
      }
      if (command == 0x44) {
        check(request[6] == 7, "RTC write length");
        const auto rtc = p::decodeRtc(request.data() + 7);
        check(rtc.valid, "invalid RTC write transmitted");
        if (!ignoreRtcWrite) {
          rtcValid = true;
          rtcSetMs = nowMs;
          rtcEpoch = epoch(rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
        }
      } else if (command == 0x85) {
        check(request[6] == 11, "configuration write length");
        uint32_t coarse = 0;
        check(p::decodeBcd(request.data() + 11, 4, true, coarse), "configuration total BCD");
        if (!ignoreConfigWrite) {
          std::copy(request.begin() + 7, request.begin() + 11, identity.tuple.begin());
          std::copy(request.begin() + 15, request.begin() + 18, identity.upper.begin());
          baud = p::baudRate(identity.tuple[0]);
          total = coarse * 10;
          fraction = 0;
        }
      } else throw std::runtime_error("unexpected command transmitted");
      offline = offlineAfterWrite;
      return dropAck || offline ? std::vector<uint8_t>{} : eb(0x9a, {});
    }
    if (request[0] == 0x68) {
      if (failRtcRead) return {};
      const auto target = p::address(identity);
      if (!std::equal(target.begin(), target.end(), request.begin() + 2)) return {};
      check(request.size() == 16 && request[9] == 1 && request[10] == 3 &&
                request[11] == 0x15 && request[12] == 0xa0,
            "unexpected normal68 command");
      std::vector<uint8_t> reply{0x68, 0x10};
      reply.insert(reply.end(), target.begin(), target.end());
      reply.insert(reply.end(), {0x81, 10, 0x15, 0xa0, request[13]});
      const auto raw = rtcBytes();
      reply.insert(reply.end(), raw.begin(), raw.end());
      reply.push_back(p::sum8(reply.data(), reply.size()));
      reply.push_back(0x16);
      return reply;
    }
    if (request[0] == identity.tuple[3]) {
      check(request.size() == 8 && request[1] == 3 && request[3] == 2 && request[5] == 2,
            "unexpected Modbus command");
      std::vector<uint8_t> reply{identity.tuple[3], 3, 4, 0, 0, 0, 0, 0, 0};
      bcd4(flow, reply.data() + 3, false);
      const auto crc = p::modbusCrc(reply.data(), 7);
      reply[7] = uint8_t(crc);
      reply[8] = uint8_t(crc >> 8);
      return reply;
    }
    return {};
  }
} meter;
void reset() {
  nowMs = 1000;
  wallBase = epoch(2026, 9, 24, 10);
  persisted = 0;
  meter = Meter{};
}
}  // namespace fake

namespace esp32m::modbus {
struct Master {
  static Master& instance() { static Master instance; return instance; }
  esp_err_t exchangeRaw(const void* request, size_t requestSize, void* response,
      size_t responseCapacity, size_t* responseSize, uint32_t baud,
      uint32_t timeout, bool (*complete)(const uint8_t*, size_t, void*), void* context) {
    check(timeout >= 1500, "driver response timeout below application minimum");
    const auto* bytes = static_cast<const uint8_t*>(request);
    const auto reply = fake::meter.respond({bytes, bytes + requestSize}, baud);
    fake::meter.transactions.back().fullCapture = !complete;
    if (reply.empty()) { *responseSize = 0; return ESP_ERR_TIMEOUT; }
    // Match the production transport: null callback captures the complete
    // bounded window, while rejected callback replies finish as TIMEOUT.
    *responseSize = std::min(reply.size(), responseCapacity);
    std::memcpy(response, reply.data(), *responseSize);
    if (fake::Meter::isDiscovery(fake::meter.transactions.back().bytes) &&
        fake::meter.discoveryTransportError != ESP_OK)
      return fake::meter.discoveryTransportError;
    if (!complete) return ESP_OK;
    if (complete(reply.data(), *responseSize, context)) return ESP_OK;
    return *responseSize == responseCapacity ? ESP_ERR_INVALID_SIZE : ESP_ERR_TIMEOUT;
  }
};
}  // namespace esp32m::modbus

// Replace only time sources. The generated declaration and methods stay intact.
#define time fake_time
#define localtime_r fake_localtime_r
#define mktime fake_mktime
#include "production.inc"
#undef time
#undef localtime_r
#undef mktime

using namespace esp32m;
struct TestSm10 : dev::Sm10 {
  using Sm10::Sm10;
  using Sm10::initSensors;
  using Sm10::pollSensors;
  using Sm10::setDeviceConfig;
  using Sm10::getDeviceConfig;
  using Sm10::getState;
  using Sm10::handleRequest;
  JsonDocument snapshot() {
    Request request;
    RequestContext context(request);
    std::unique_ptr<JsonDocument> result(getState(context));
    return *result;
  }
  JsonDocument settings() {
    Request request;
    RequestContext context(request);
    std::unique_ptr<JsonDocument> result(getDeviceConfig(context));
    return *result;
  }
};

void checkIdentity(const JsonDocument& settings, const fake::p::Identity& identity,
                   const char* message) {
  for (size_t i = 0; i < identity.tuple.size(); ++i)
    check(settings["tuple"][i] == identity.tuple[i], message);
  for (size_t i = 0; i < identity.upper.size(); ++i)
    check(settings["upper"][i] == identity.upper[i], message);
}

void restore(TestSm10& meter, const JsonDocument& settings) {
  Request request("", true);
  request.payload.set(settings);
  RequestContext context(request);
  const auto before = fake::meter.transactions.size();
  check(meter.setDeviceConfig(context) && context.errors.error == ESP_OK &&
            fake::meter.transactions.size() == before,
        "restoring discovered address accessed hardware");
}

void discoveryCompatibility() {
  for (const auto baud : {1200U, 2400U, 4800U, 9600U}) {
    for (const auto id : {1U, 7U, 234U, 236U, 247U}) {
      fake::reset();
      fake::meter.baud = baud;
      fake::p::baudSelector(baud, fake::meter.identity.tuple[0]);
      // Noncanonical selectors also mean 1200 baud and must be retained.
      if (baud == 1200) fake::meter.identity.tuple[0] = 0x11;
      fake::meter.identity.tuple[1] = uint8_t(id ^ 0xeb);
      fake::meter.identity.tuple[2] = uint8_t(id ^ 0xfe);
      fake::meter.identity.tuple[3] = uint8_t(id);
      fake::meter.identity.upper = {0xfe, uint8_t(id), 0xeb};
      fake::meter.setValidRtc();
      TestSm10 meter(uint8_t(id), baud);
      check(meter.initSensors() && fake::meter.discoveries() == 1 &&
                fake::meter.count(0x8c) == 4 && fake::meter.count(0x8b) == 1,
            "arbitrary address was not discovered and exactly confirmed");
      check(fake::persisted == 1 && fake::meter.writes() == 0 &&
                fake::meter.rtcReads() == 0,
            "discovery did not cache once locally or wrote to meter");
      checkIdentity(meter.settings(), fake::meter.identity, "discovered address bytes lost");
      for (const auto& transaction : fake::meter.transactions) {
        check(transaction.baud == baud, "discovery changed configured baud");
        check(transaction.fullCapture == fake::Meter::isDiscovery(transaction.bytes),
              "discovery did not collect full window or normal request lost validator");
      }
      check(meter.pollSensors() && fake::meter.writes() == 0 &&
                fake::meter.discoveries() == 1,
            "normal poll failed after discovery or repeated wildcard");
      const auto saved = meter.settings();
      TestSm10 rebooted;
      restore(rebooted, saved);
      check(rebooted.initSensors() && rebooted.pollSensors() &&
                fake::meter.discoveries() == 1 && fake::persisted == 1,
            "saved address required rediscovery or redundant persistence");
    }
  }

  fake::reset();
  fake::meter.identity.upper = {0x12, 0x34, 0x56};
  TestSm10 defaultTuple;
  check(defaultTuple.initSensors() && fake::meter.discoveries() == 0 &&
            fake::persisted == 1, "direct identity did not cache newly learned upper bytes");
  checkIdentity(defaultTuple.settings(), fake::meter.identity, "direct identity lost upper bytes");
  check(defaultTuple.initSensors() && fake::persisted == 1,
        "unchanged direct identity persisted repeatedly");

  for (const unsigned prefix : {0U, 1U, 7U}) {
    fake::reset();
    fake::meter.identity.tuple = {0x96, 0xfe, 0xeb, 1};
    auto reply = fake::meter.identityReply();
    reply.erase(reply.begin());
    reply.insert(reply.begin(), prefix, 0xfe);
    fake::meter.discoveryReply = reply;
    fake::meter.overrideDiscovery = true;
    TestSm10 meter;
    check(meter.initSensors(), "valid identity with optional FE preamble rejected");
  }
}

void repairIdentityChecksum(std::vector<uint8_t>& frame) {
  check(frame.size() >= 2 && frame[0] == 0xfe && frame[1] == 0xeb,
        "invalid identity test fixture");
  frame.back() = fake::p::sum8(frame.data() + 1, frame.size() - 2);
}

void discoveryRejectsAmbiguity() {
  // Exercise the whole raw receive window. A streaming first-frame callback
  // would miss the extra replies and incorrectly adopt some of these inputs.
  const std::vector<std::string> cases = {
      "silent", "wrong-id", "wrong-baud", "signature", "inner-tuple", "selector",
      "length", "prefix-noise", "suffix-noise", "suffix-fe", "echo", "truncated",
      "checksum", "duplicate", "second-meter", "full-buffer", "transport-error"};
  for (const auto& fault : cases) {
    fake::reset();
    fake::meter.identity.tuple = {0x96, 0x20, 0x56, 1};
    fake::meter.identity.upper = {0x13, 0x57, 0x9b};
    if (fault == "wrong-id") fake::meter.identity.tuple[3] = 2;
    if (fault == "wrong-baud") fake::meter.identity.tuple[0] = 0x48;
    if (fault == "signature") fake::meter.identity.signature[4] ^= 1;
    auto reply = fake::meter.identityReply();
    if (fault == "silent") reply.clear();
    if (fault == "inner-tuple") { reply[15] ^= 1; repairIdentityChecksum(reply); }
    if (fault == "selector") { reply[21] ^= 1; repairIdentityChecksum(reply); }
    if (fault == "length") { reply[7] = 13; repairIdentityChecksum(reply); }
    if (fault == "prefix-noise") reply.insert(reply.begin(), 0x00);
    if (fault == "suffix-noise") reply.push_back(0x00);
    if (fault == "suffix-fe") reply.push_back(0xfe);
    if (fault == "echo") {
      const std::vector<uint8_t> echo{0xeb, 0x8c, 0x12, 0x34, 0x56, 0x78, 0, 0x8b};
      reply.insert(reply.begin(), echo.begin(), echo.end());
    }
    if (fault == "truncated") reply.pop_back();
    if (fault == "checksum") reply.back() ^= 1;
    if (fault == "duplicate") {
      const auto duplicate = reply;
      reply.insert(reply.end(), duplicate.begin(), duplicate.end());
    }
    if (fault == "second-meter") {
      auto other = fake::meter;
      other.identity.tuple[1] ^= 0x55;
      const auto second = other.identityReply();
      reply.insert(reply.end(), second.begin(), second.end());
    }
    if (fault == "full-buffer") reply.insert(reply.begin(), 256 - reply.size(), 0xfe);
    if (fault == "transport-error") fake::meter.discoveryTransportError = ESP_FAIL;
    fake::meter.overrideDiscovery = true;
    fake::meter.discoveryReply = reply;
    TestSm10 meter;
    const auto original = meter.settings();
    check(!meter.initSensors(), ("discovery accepted " + fault).c_str());
    check(fake::meter.discoveries() == 1 && fake::meter.count(0x8b) == 0 &&
              fake::persisted == 0 && fake::meter.writes() == 0,
          ("rejected discovery adopted/wrote/retried " + fault).c_str());
    check(meter.settings()["tuple"][1] == original["tuple"][1] &&
              meter.settings()["upper"][0] == original["upper"][0],
          "rejected discovery changed cached address");
    check(!meter.initSensors() && fake::meter.discoveries() == 1,
          "failed discovery was retried before throttle deadline");
  }

  fake::reset();
  fake::meter.identity.signature[0] ^= 1;
  TestSm10 knownButUnsupported;
  check(!knownButUnsupported.initSensors() && fake::meter.discoveries() == 0 &&
            fake::persisted == 0 && fake::meter.writes() == 0,
        "unsupported addressed identity triggered wildcard fallback");
}

void discoveryConfirmationAndReplacement() {
  for (const auto fault : {"identity-timeout", "identity-changed", "fine-timeout", "fine-layout"}) {
    fake::reset();
    fake::meter.identity.tuple = {0x96, 0x20, 0x56, 1};
    fake::meter.identity.upper = {0x13, 0x57, 0x9b};
    fake::meter.failUnicastIdentity = !std::strcmp(fault, "identity-timeout");
    fake::meter.failFineRead = !std::strcmp(fault, "fine-timeout");
    if (!std::strcmp(fault, "identity-changed")) {
      auto different = fake::meter;
      different.identity.upper[0] ^= 1;
      fake::meter.overrideUnicastIdentity = true;
      fake::meter.unicastIdentityReply = different.identityReply();
    }
    if (!std::strcmp(fault, "fine-layout")) {
      fake::meter.overrideFine = true;
      std::vector<uint8_t> invalid(18);
      invalid[0] = 0xfa;
      fake::meter.fineReply = fake::meter.eb(0x9b, invalid);
    }
    TestSm10 meter;
    check(!meter.initSensors() && fake::persisted == 0 && fake::meter.writes() == 0,
          "failed discovery confirmation adopted address or wrote settings");
    check(meter.settings()["tuple"][1] == 0 && meter.settings()["upper"][0] == 0,
          "failed confirmation did not restore original address");
    fake::meter.failUnicastIdentity = false;
    fake::meter.overrideUnicastIdentity = false;
    fake::meter.failFineRead = false;
    fake::meter.overrideFine = false;
    check(!meter.initSensors() && fake::meter.discoveries() == 1,
          "failed confirmation retained candidate address or bypassed throttle");
    fake::nowMs += 60001;
    check(meter.initSensors() && fake::meter.discoveries() == 2 && fake::persisted == 1,
          "discovery did not recover after failure throttle expired");
  }

  fake::reset();
  fake::meter.identity.tuple = {0x96, 0x20, 0x56, 1};
  fake::meter.setValidRtc();
  TestSm10 meter;
  check(meter.initSensors() && meter.pollSensors(), "first meter discovery failed");
  fake::meter.identity.tuple = {0x96, 0xeb, 0xfe, 1};
  fake::meter.identity.upper = {0x54, 0x32, 0x10};
  check(!meter.pollSensors(), "replaced meter incorrectly answered old tuple");
  check(!meter.initSensors() && fake::meter.discoveries() == 1,
        "replacement bypassed successful discovery throttle");
  fake::nowMs += 60001;
  check(meter.initSensors() && meter.pollSensors() && fake::meter.discoveries() == 2 &&
            fake::persisted == 2 && fake::meter.writes() == 0,
        "replacement meter was not rediscovered after throttle");
  checkIdentity(meter.settings(), fake::meter.identity, "replacement address not retained");
}

void discoveryStaysOutOfWrites() {
  fake::reset();
  fake::meter.identity.tuple = {0x96, 0x20, 0x56, 1};
  TestSm10 meter;
  Request total("set-total");
  total.payload["total"] = 1.0;
  total.payload["confirm"] = true;
  check(meter.handleRequest(total), "total request not handled");
  check(total.error == ESP_ERR_TIMEOUT,
        ("total preflight error " + std::to_string(total.error)).c_str());
  check(fake::meter.discoveries() == 0 && fake::meter.writes() == 0,
        "total request discovered and adopted an unknown meter");
  Request update;
  deserializeJson(update.payload, R"({"address":7,"baud":4800,"confirm":true})");
  RequestContext context(update);
  check(!meter.setDeviceConfig(context) && context.errors.error == ESP_ERR_TIMEOUT &&
            fake::meter.discoveries() == 0 && fake::meter.writes() == 0,
        "configuration request discovered and adopted an unknown meter");

  // An uncertain setting must probe only the original and intended exact
  // addresses, even if another responding meter could satisfy discovery.
  fake::reset();
  fake::meter.offlineAfterWrite = true;
  TestSm10 pending;
  RequestContext pendingContext(update);
  check(pending.setDeviceConfig(pendingContext) && pendingContext.errors.error != ESP_OK &&
            fake::meter.writes() == 1, "pending recovery fixture did not write once");
  fake::meter.offline = false;
  fake::meter.identity.tuple[1] = 0x20;
  fake::nowMs += 60001;
  check(!pending.initSensors() && fake::meter.discoveries() == 0 &&
            fake::meter.writes() == 1 && pending.settings()["pendingAddress"] == 7,
        "uncertain write recovery used discovery or discarded pending state");
}

void flowUnits() {
  fake::reset();
  fake::meter.setValidRtc();
  TestSm10 meter;
  // The recovered firmware returns whole L/h. Check the complete wire-to-state
  // path, including zero and 1 L/h, to prevent an extra /100 or integer division.
  const struct { uint32_t raw; double m3PerHour; } cases[] = {
      {0, 0.0}, {1, 0.001}, {2362, 2.362}, {12345, 12.345}};
  for (const auto& test : cases) {
    fake::meter.flow = test.raw;
    fake::nowMs += 5000;
    check(meter.pollSensors(), "flow unit regression poll failed");
    const auto state = meter.snapshot();
    check(state["flow"].is<double>() &&
              std::abs(state["flow"].as<double>() - test.m3PerHour) < 1e-9,
          "raw L/h was not converted to m3/h exactly once");
  }
  check(fake::meter.writes() == 0, "flow polling wrote to the meter");
}

void rtcPolicy() {
  fake::reset();
  TestSm10 meter;
  check(meter.pollSensors(), "initial poll failed");
  check(fake::meter.count(0x44) == 1 && fake::meter.rtcReads() >= 2 &&
            meter.snapshot()["rtcSync"].as<std::string>() == "synced",
        "invalid RTC must be set and independently verified");
  check(fake::meter.total == 736 && fake::meter.fraction == 8321,
        "RTC sync affected accumulated total");
  check(fake::meter.count(0x40) == 0 && fake::meter.count(0x41) == 0 &&
            fake::meter.count(0x42) == 0 && fake::meter.count(0x43) == 0,
        "history was polled without user demand");
  const auto reads = fake::meter.rtcReads();
  fake::nowMs += 5000;
  check(meter.pollSensors() && fake::meter.rtcReads() == reads,
        "RTC checked before periodic deadline");
  fake::meter.rtcValid = false;  // Independent meter restart, ESP32 stays up.
  fake::nowMs += 60000;
  check(meter.pollSensors() && fake::meter.count(0x44) == 2,
        "periodic check missed independent meter restart");

  fake::reset();
  fake::meter.setValidRtc();
  TestSm10 valid;
  check(valid.pollSensors() && fake::meter.writes() == 0 &&
            valid.snapshot()["rtcSync"].as<std::string>() == "valid",
        "valid RTC was overwritten");
  fake::reset();
  fake::wallBase = 0;
  TestSm10 unsynchronized;
  check(unsynchronized.pollSensors() && fake::meter.writes() == 0 &&
            unsynchronized.snapshot()["rtcSync"].as<std::string>() == "waiting-for-time",
        "invalid ESP32 time was sent to meter");
  fake::wallBase = fake::epoch(2026, 9, 24, 10);
  fake::nowMs += 60000;
  check(unsynchronized.pollSensors() && fake::meter.count(0x44) == 1,
        "valid host time becoming available was missed");

  fake::reset();
  fake::meter.failRtcRead = true;
  TestSm10 failed;
  check(failed.pollSensors() && fake::meter.writes() == 0 &&
            failed.snapshot()["rtcSync"].as<std::string>() == "read-failed",
        "RTC read failure triggered a write");
  fake::reset();
  fake::meter.failFineRead = true;
  TestSm10 unreadable;
  check(!unreadable.pollSensors() && fake::meter.writes() == 0,
        "meter read failure triggered a write");
  fake::reset();
  fake::meter.dropAck = true;
  TestSm10 noAck;
  check(noAck.pollSensors() && fake::meter.count(0x44) == 1 &&
            noAck.snapshot()["rtcSync"].as<std::string>() == "synced",
        "RTC ACK loss caused failure/retransmission despite good readback");
  fake::reset();
  fake::meter.ignoreRtcWrite = true;
  TestSm10 lyingAck;
  check(lyingAck.pollSensors() && fake::meter.count(0x44) == 1 &&
            lyingAck.snapshot()["rtcSync"].as<std::string>() == "sync-failed",
        "RTC ACK without applied calendar was accepted as success");
}

void rolloverAndRestoration() {
  fake::reset();
  fake::nowMs = (uint64_t(1) << 32) - 10000;
  fake::meter.setValidRtc();
  TestSm10 meter;
  check(meter.pollSensors(), "poll near 49-day rollover");
  const auto first = meter.snapshot();
  const auto firstSampledAt = first["sampledAtMs"].as<uint64_t>();
  const auto firstRtcAt = first["rtcSampledAtMs"].as<uint64_t>();
  check(firstSampledAt > 0 && firstRtcAt >= firstSampledAt &&
            first["age"].isUnbound() && first["rtcAge"].isUnbound(),
        "meter and RTC must expose independent exact acquisition timestamps");
  fake::nowMs += 37;
  check(meter.snapshot()["sampledAtMs"] == firstSampledAt &&
            meter.snapshot()["rtcSampledAtMs"] == firstRtcAt,
        "state reads must not advance source acquisition times");
  const auto reads = fake::meter.rtcReads();
  fake::nowMs += 20000;
  check(meter.pollSensors() && fake::meter.rtcReads() == reads,
        "RTC deadline wrapped early at 49 days");
  check(meter.snapshot()["sampledAtMs"].as<uint64_t>() > (uint64_t(1) << 32) &&
            meter.snapshot()["rtcSampledAtMs"] == firstRtcAt,
        "unchanged meter samples renew 64-bit acquisition time without renewing unpolled RTC");
  fake::nowMs += 45000;
  fake::meter.rtcValid = false;
  check(meter.pollSensors() && fake::meter.count(0x44) == 1,
        "RTC deadline failed across 49 days");

  fake::reset();
  TestSm10 restored;
  check(restored.snapshot()["sampledAtMs"] == -1 &&
            restored.snapshot()["rtcSampledAtMs"] == -1,
        "never-read measurement and RTC sentinels must remain unknown");
  Request request("", true);
  deserializeJson(request.payload,
      R"({"address":7,"baud":4800,"tuple":[72,17,34,7],"upper":[51,68,85]})");
  RequestContext context(request);
  check(restored.setDeviceConfig(context) && context.errors.error == ESP_OK &&
            fake::meter.transactions.empty(), "saved config restoration accessed hardware");
  const auto cfg = restored.settings();
  check(cfg["address"] == 7 && cfg["baud"] == 4800 && cfg["upper"][2] == 85 &&
            cfg["tuple"][1] == 17, "opaque saved identity lost");
  fake::meter.identity.tuple = {0x48, 17, 34, 7};
  fake::meter.identity.upper = {51, 68, 85};
  fake::meter.baud = 4800;
  check(restored.initSensors(), "restored opaque identity could not be addressed");
}

void inputAndHistory() {
  fake::reset();
  TestSm10 meter;
  for (const char* payload : {R"({"address":2,"baud":4800})",
      R"({"address":235,"baud":4800,"confirm":true})",
      R"({"address":2,"baud":19200,"confirm":true})",
      R"({"address":1.5,"baud":9600,"confirm":true})"}) {
    Request request;
    deserializeJson(request.payload, payload);
    RequestContext context(request);
    check(!meter.setDeviceConfig(context) && context.errors.error == ESP_ERR_INVALID_ARG &&
              fake::meter.transactions.empty(), "invalid/unconfirmed config touched hardware");
  }
  for (const char* payload : {R"({"total":1.2345})", R"({"total":0.00001,"confirm":true})",
      R"({"total":10000,"confirm":true})", R"({"total":-1,"confirm":true})"}) {
    Request request("set-total");
    deserializeJson(request.payload, payload);
    check(meter.handleRequest(request) && request.error == ESP_ERR_INVALID_ARG &&
              fake::meter.transactions.empty(), "invalid/unconfirmed total touched hardware");
  }
  Request broadcast("set-total");
  broadcast.broadcast = true;
  deserializeJson(broadcast.payload, R"({"total":1.2345,"confirm":true})");
  check(meter.handleRequest(broadcast) && broadcast.error == ESP_ERR_INVALID_ARG &&
            fake::meter.transactions.empty(), "broadcast total request touched hardware");
  fake::meter.setValidRtc();
  check(meter.pollSensors() && fake::meter.count(0x40) == 0, "automatic history reads");
  Request history("history");
  history.payload["bank"] = "hourly";
  check(meter.handleRequest(history) && history.error == ESP_OK &&
            history.response["values"].size() == 48 &&
            history.response["values"][47].as<double>() == 0.0047 &&
            fake::meter.count(0x40) == 1 && fake::meter.writes() == 0,
        "on-demand history response/order");
  const auto calls = fake::meter.transactions.size();
  history.payload["bank"] = "unsupported";
  check(meter.handleRequest(history) && history.error == ESP_ERR_INVALID_ARG &&
            fake::meter.transactions.size() == calls, "invalid history accessed hardware");
}

void totalAndConfiguration() {
  fake::reset();
  fake::meter.dropAck = true;
  TestSm10 meter;
  Request total("set-total");
  total.payload["confirm"] = true;
  total.payload["total"] = 12.3456;
  check(meter.handleRequest(total) && total.error == ESP_OK &&
            fake::meter.count(0x85) == 1 && fake::meter.total == 1234560 &&
            fake::meter.fraction == 0 &&
            std::abs(total.response["total"].as<double>() - 12.3456) < 1e-10,
        "total write must be sent once and verified despite missing ACK");
  fake::reset();
  fake::meter.flow = 1;
  TestSm10 flowing;
  check(flowing.handleRequest(total) && total.error == ESP_ERR_INVALID_STATE &&
            fake::meter.writes() == 0, "flowing meter total was overwritten");
  fake::reset();
  fake::meter.ignoreConfigWrite = true;
  TestSm10 lyingAck;
  check(lyingAck.handleRequest(total) && total.error == ESP_ERR_INVALID_RESPONSE &&
            fake::meter.count(0x85) == 1 && fake::meter.total == 736,
        "total ACK without applied value was accepted as success");

  fake::reset();
  fake::meter.identity.upper = {0x11, 0x22, 0x33};
  fake::meter.dropAck = true;
  TestSm10 config;
  Request request;
  deserializeJson(request.payload, R"({"address":7,"baud":4800,"confirm":true})");
  RequestContext context(request);
  check(config.setDeviceConfig(context) && context.errors.error == ESP_OK &&
            fake::meter.count(0x85) == 1 && fake::meter.baud == 4800 &&
            fake::meter.identity.tuple[3] == 7 &&
            fake::meter.identity.upper == std::array<uint8_t, 3>{0x11, 0x22, 0x33} &&
            fake::meter.total == 730 && fake::meter.fraction == 0,
        "config change failed to preserve coarse total/upper address");
  check(config.settings()["address"] == 7 && config.settings()["baud"] == 4800,
        "driver did not retain verified settings");

  fake::reset();
  fake::meter.offlineAfterWrite = true;
  TestSm10 pending;
  RequestContext pendingContext(request);
  check(pending.setDeviceConfig(pendingContext) && pendingContext.errors.error != ESP_OK &&
            fake::meter.count(0x85) == 1, "uncertain config did not report failure");
  const auto saved = pending.settings();
  check(saved["address"] == 1 && saved["baud"] == 9600 &&
            saved["pendingAddress"] == 7 && saved["pendingBaud"] == 4800,
        "uncertain config recovery identity not retained");
  fake::meter.offline = false;
  fake::meter.setValidRtc();
  check(pending.pollSensors() && fake::meter.count(0x85) == 1 &&
            pending.settings()["address"] == 7 &&
            pending.settings()["pendingAddress"].isUnbound(),
        "pending recovery retransmitted write or missed new address");

  // A baud byte other than 96/48/24 is an accepted 1200 selector. ID-only
  // changes preserve it, including during uncertain-outcome recovery.
  fake::reset();
  fake::meter.identity.tuple = {0x11, 0, 0, 1};
  fake::meter.baud = 1200;
  fake::meter.offlineAfterWrite = true;
  TestSm10 alternate;
  Request restore("", true);
  deserializeJson(restore.payload,
      R"({"address":1,"baud":1200,"tuple":[17,0,0,1],"upper":[0,0,0]})");
  RequestContext restoreContext(restore);
  check(alternate.setDeviceConfig(restoreContext), "restore noncanonical 1200 selector");
  Request update;
  deserializeJson(update.payload, R"({"address":7,"baud":1200,"confirm":true})");
  RequestContext updateContext(update);
  check(alternate.setDeviceConfig(updateContext) && updateContext.errors.error != ESP_OK &&
            fake::meter.identity.tuple == fake::p::Tuple{0x11, 0, 0, 7},
        "ID-only update changed existing 1200 selector");
  // Recreate ESP driver from saved pending configuration, as after ESP reboot.
  const auto pendingConfig = alternate.settings();
  TestSm10 rebooted;
  Request reload("", true);
  reload.payload.set(pendingConfig);
  RequestContext reloadContext(reload);
  const auto previousCalls = fake::meter.transactions.size();
  check(rebooted.setDeviceConfig(reloadContext) &&
            fake::meter.transactions.size() == previousCalls,
        "pending restoration accessed hardware");
  fake::meter.offline = false;
  fake::meter.setValidRtc();
  check(rebooted.pollSensors() && fake::meter.count(0x85) == 1 &&
            rebooted.settings()["address"] == 7 && rebooted.settings()["tuple"][0] == 0x11,
        "pending recovery lost noncanonical 1200 selector");
}

void decimalTotalRequests() {
  fake::reset();
  TestSm10 meter;
  struct TotalRequest { const char* json; uint32_t coarse; };
  // Deserialize actual API JSON, rather than assigning a double directly into
  // a document: ArduinoJson may store decimal JSON numbers as float internally.
  for (const auto& value : std::vector<TotalRequest>{
      {R"({"total":4,"confirm":true})", 40000},
      {R"({"total":"0.2","confirm":true})", 2000},
      {R"({"total":"0.2000","confirm":true})", 2000},
      {R"({"total":"0.0001","confirm":true})", 1},
      {R"({"total":"12.3456","confirm":true})", 123456},
      {R"({"total":"9999.9","confirm":true})", 99999000},
      {R"({"total":"9999.9999","confirm":true})", 99999999},
      {R"({"total":"0","confirm":true})", 0}}) {
    Request request("set-total");
    check(!deserializeJson(request.payload, value.json), "valid total JSON did not parse");
    const auto writes = fake::meter.count(0x85);
    check(meter.handleRequest(request) && request.error == ESP_OK &&
              fake::meter.count(0x85) == writes + 1 &&
              fake::meter.total == value.coarse * 10 && fake::meter.fraction == 0,
          ("API decimal total did not produce exact meter counts: " +
           std::string(value.json)).c_str());
    const auto write = std::find_if(fake::meter.transactions.rbegin(),
        fake::meter.transactions.rend(), [](const auto& transaction) {
          return transaction.bytes[0] == 0xeb && transaction.bytes[1] == 0x85;
        });
    uint32_t encoded = 0;
    check(write != fake::meter.transactions.rend() &&
              fake::p::decodeBcd(write->bytes.data() + 11, 4, true, encoded) &&
              encoded == value.coarse,
          "decimal API request changed total in wire BCD encoding");
  }

  // Invalid or unconfirmed requests must fail before even read-only preflight.
  fake::reset();
  TestSm10 guarded;
  for (const char* json : {
      R"({"total":"0.2"})", R"({"total":"0.2","confirm":false})",
      R"({"total":"0.2","confirm":1})", R"({"total":"0.2","confirm":"true"})",
      R"({"confirm":true})", R"({"total":null,"confirm":true})",
      R"({"total":true,"confirm":true})", R"({"total":{},"confirm":true})",
      R"({"total":[0.2],"confirm":true})", R"({"total":"","confirm":true})",
      R"({"total":"0.2\u00005","confirm":true})",
      R"({"total":"0.2\u0000","confirm":true})",
      R"({"total":"0.2x","confirm":true})", R"({"total":"0.00001","confirm":true})",
      R"({"total":"0.20000","confirm":true})", R"({"total":"10000","confirm":true})",
      R"({"total":"9999999999999999999999999999","confirm":true})",
      R"({"total":"2e-1","confirm":true})", R"({"total":"+0.2","confirm":true})",
      R"({"total":"-0.2","confirm":true})", R"({"total":" 0.2","confirm":true})",
      R"({"total":"0.2 ","confirm":true})", R"({"total":"1.","confirm":true})",
      R"({"total":".2","confirm":true})",
      R"({"total":0.2,"confirm":true})", R"({"total":9999.9,"confirm":true})"}) {
    Request request("set-total");
    check(!deserializeJson(request.payload, json), "guard test JSON did not parse");
    check(guarded.handleRequest(request) && request.error == ESP_ERR_INVALID_ARG &&
              fake::meter.transactions.empty(),
          ("invalid total JSON touched hardware: " + std::string(json)).c_str());
  }
  Request broadcast("set-total");
  broadcast.broadcast = true;
  check(!deserializeJson(broadcast.payload, R"({"total":"0.2","confirm":true})"),
        "broadcast total JSON did not parse");
  check(guarded.handleRequest(broadcast) && broadcast.error == ESP_ERR_INVALID_ARG &&
            fake::meter.transactions.empty(), "broadcast decimal total touched hardware");
}

int main() {
  try {
    discoveryCompatibility();
    discoveryRejectsAmbiguity();
    discoveryConfirmationAndReplacement();
    discoveryStaysOutOfWrites();
    flowUnits();
    rtcPolicy();
    rolloverAndRestoration();
    inputAndHistory();
    totalAndConfiguration();
    decimalTotalRequests();
    std::cout << "SM10 driver: 10 scenario groups passed\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
