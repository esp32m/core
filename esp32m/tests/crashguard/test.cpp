#include <ArduinoJson.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#define CONFIG_ESP_COREDUMP_ENABLE 0
#define CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH 0
#define RTC_NOINIT_ATTR
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = 1,
    ESP_ERR_INVALID_STATE = 2, ESP_ERR_NOT_FOUND = 3,
    ESP_ERR_NOT_SUPPORTED = 4, ESP_ERR_INVALID_SIZE = 5;
enum esp_reset_reason_t {
  ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC,
  ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT, ESP_RST_SDIO
};
constexpr int ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1,
    ESP_PARTITION_SUBTYPE_APP_OTA_0 = 16, ESP_PARTITION_SUBTYPE_APP_OTA_1 = 17;
struct esp_partition_t {
  int type, subtype;
  uint32_t address, size;
  char label[16];
};
struct esp_app_desc_t {
  char version[32], project_name[32], date[16], time[16];
  uint8_t app_elf_sha256[32];
};
enum esp_ota_img_states_t {
  ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID,
  ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED
};
using nvs_handle_t = int;
constexpr int NVS_READONLY = 0, NVS_READWRITE = 1;

namespace fake {
uint32_t now = 0;
esp_partition_t parts[] = {{0, 16, 0x10000, 0x100000, "ota_0"},
                           {0, 17, 0x110000, 0x100000, "ota_1"}};
esp_app_desc_t desc[2];
int running = 0, selected = 0, setBootResult = ESP_OK;
esp_reset_reason_t reason = ESP_RST_POWERON;
bool descriptorFailure = false, noOther = false;
unsigned descriptors = 0, stateEvents = 0, appRestarts = 0;
std::vector<std::string> actions;
std::map<std::string, std::vector<uint8_t>> nvs;
std::map<std::string, int> failSet, failCommit;
struct Transaction { std::string key; std::vector<uint8_t> bytes; };
std::map<int, Transaction> transactions;
int nextHandle = 0;
std::function<void(const std::string&)> afterCommit;
std::function<void()> afterSetBoot;
struct Restart {};
struct PowerCut {};
void version(int slot, const char* ver, uint8_t hash) {
  desc[slot] = {};
  std::strncpy(desc[slot].version, ver, sizeof(desc[slot].version) - 1);
  std::memset(desc[slot].app_elf_sha256, hash, 32);
}
bool fail(std::map<std::string, int>& failures, const std::string& key) {
  auto& count = failures[key];
  if (count == 0) return false;
  if (count > 0) --count;
  return true;
}
}

uint32_t millis() { return fake::now; }
esp_reset_reason_t esp_reset_reason() { return fake::reason; }
const esp_partition_t* esp_ota_get_running_partition() {
  return &fake::parts[fake::running];
}
const esp_partition_t* esp_ota_get_boot_partition() {
  return &fake::parts[fake::selected];
}
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* p) {
  return fake::noOther ? nullptr : &fake::parts[p == &fake::parts[0] ? 1 : 0];
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* p) {
  fake::actions.push_back("select");
  if (fake::setBootResult != ESP_OK) return fake::setBootResult;
  fake::selected = p == &fake::parts[0] ? 0 : 1;
  if (fake::afterSetBoot) fake::afterSetBoot();
  return ESP_OK;
}
esp_err_t esp_ota_get_partition_description(const esp_partition_t* p,
                                           esp_app_desc_t* desc) {
  ++fake::descriptors;
  if (fake::descriptorFailure) return ESP_FAIL;
  *desc = fake::desc[p == &fake::parts[0] ? 0 : 1];
  return ESP_OK;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t*) {
  return ESP_ERR_NOT_SUPPORTED;
}
const esp_partition_t* esp_partition_find_first(int, int subtype, const char*) {
  return &fake::parts[subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? 0 : 1];
}
esp_err_t esp_partition_read(const esp_partition_t*, size_t, void*, size_t) {
  return ESP_FAIL;
}
void esp_restart() { fake::actions.push_back("restart"); throw fake::Restart{}; }
esp_err_t nvs_open(const char*, int, nvs_handle_t* handle) {
  *handle = ++fake::nextHandle;
  fake::transactions[*handle] = {};
  return ESP_OK;
}
esp_err_t nvs_get_blob(int, const char* key, void* target, size_t* size) {
  const auto found = fake::nvs.find(key);
  if (found == fake::nvs.end()) return ESP_ERR_NOT_FOUND;
  if (found->second.size() > *size) return ESP_ERR_INVALID_SIZE;
  *size = found->second.size();
  std::memcpy(target, found->second.data(), *size);
  return ESP_OK;
}
esp_err_t nvs_set_blob(int handle, const char* key, const void* source, size_t size) {
  fake::actions.push_back(std::string("set:") + key);
  if (fake::fail(fake::failSet, key)) return ESP_FAIL;
  fake::transactions[handle] = {key, std::vector<uint8_t>(
      static_cast<const uint8_t*>(source), static_cast<const uint8_t*>(source) + size)};
  return ESP_OK;
}
esp_err_t nvs_commit(int handle) {
  const auto transaction = fake::transactions[handle];
  if (fake::fail(fake::failCommit, transaction.key)) return ESP_FAIL;
  fake::nvs[transaction.key] = transaction.bytes;
  fake::actions.push_back("commit:" + transaction.key);
  if (fake::afterCommit) fake::afterCommit(transaction.key);
  return ESP_OK;
}
void nvs_close(int handle) { fake::transactions.erase(handle); }
template<class... T> void fakeLog(T...) {}
#define logW(...) fakeLog(__VA_ARGS__)
#define logI(...) fakeLog(__VA_ARGS__)

namespace esp32m {
using ::millis;
std::string hex_encode(const uint8_t* bytes, size_t size) {
  std::string result;
  constexpr char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < size; ++i) {
    result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15];
  }
  return result;
}
namespace json {
template<class T> void to(JsonObject node, const char* key, const T value) {
  node[key] = value;
}
template<class T> bool from(JsonVariantConst node, T& value, bool* changed) {
  if (node.isNull()) return false;
  const T next = node.as<T>();
  if (next != value) { value = next; if (changed) *changed = true; }
  return true;
}
std::unique_ptr<JsonDocument> loadFromFile(const char*) { return nullptr; }
void saveToFile(const char*, JsonVariantConst) {}
}
namespace log { struct Loggable { virtual ~Loggable() = default;
  virtual const char* name() const = 0; }; }
namespace stream { struct Reader { virtual ~Reader() = default;
  virtual size_t read(uint8_t*, size_t) = 0; }; }
namespace ui {
struct AssetInfo { const char* path; const char* type; void* a; void* b; };
struct ReadableAsset { virtual ~ReadableAsset() = default;
  void init(const AssetInfo&) {}
  virtual std::unique_ptr<stream::Reader> createReader() const = 0; };
}
struct Ui { static Ui& instance() { static Ui ui; return ui; }
  void addAsset(ui::ReadableAsset*) {} };
struct Event { enum Kind { None, Init0, Inited, Periodic, Done } kind = None; };
enum class DoneReason { Restart };
struct EventInit { static bool is(Event& e, int) { return e.kind == Event::Init0; } };
struct EventInited { static bool is(Event& e) { return e.kind == Event::Inited; } };
struct EventPeriodic { static bool is(Event& e) { return e.kind == Event::Periodic; } };
struct EventDone { static bool is(Event& e, DoneReason*) { return e.kind == Event::Done; } };
struct Request { const char* operation = ""; int response = -123;
  bool is(const char* name) { return !std::strcmp(name, operation); }
  void respond(esp_err_t result = ESP_OK) { response = result; } };
struct RequestContext { JsonVariantConst data; };
struct AppObject : log::Loggable {
  virtual void handleEvent(Event&) {}
  virtual bool handleRequest(Request&) { return false; }
  virtual JsonDocument* getState(RequestContext&) { return nullptr; }
  virtual JsonDocument* getInfo(RequestContext&) { return nullptr; }
};
struct App { static App& instance() { static App app; return app; }
  void restart() { ++fake::appRestarts; } };
struct EventStateChanged {
  static void publish(AppObject*) { ++fake::stateEvents; }
};
}

// Expose only access control for the harness; all implementation bodies remain
// unchanged and execute against the platform doubles above.
#define private public
#include "production.inc"
#undef private

using esp32m::debug::CrashGuard;
using esp32m::debug::NvsState;
using esp32m::debug::s_rtc;
using esp32m::debug::rtcReset;

void reset() {
  fake::now = 0; fake::running = fake::selected = 0;
  fake::setBootResult = ESP_OK; fake::reason = ESP_RST_POWERON;
  fake::descriptorFailure = fake::noOther = false;
  fake::descriptors = fake::stateEvents = fake::appRestarts = 0;
  fake::actions.clear(); fake::nvs.clear(); fake::transactions.clear();
  fake::failSet.clear(); fake::failCommit.clear();
  fake::afterCommit = {}; fake::afterSetBoot = {};
  fake::version(0, "same-version", 0xaa);
  fake::version(1, "same-version", 0xbb);
  s_rtc = {};
}
std::unique_ptr<CrashGuard> boot(int slot, esp_reset_reason_t reason = ESP_RST_SW,
                               bool powerCut = false) {
  fake::running = slot; fake::reason = reason; fake::now = 0;
  if (powerCut) s_rtc = {};
  return std::unique_ptr<CrashGuard>(new CrashGuard());
}
JsonDocument state(CrashGuard& guard, bool compact = true) {
  JsonDocument query;
  if (compact) query["view"] = "recovery";
  esp32m::RequestContext context{query.as<JsonVariantConst>()};
  std::unique_ptr<JsonDocument> result(guard.getState(context));
  return *result;
}
void status(CrashGuard& guard, const char* expected, bool active) {
  const auto doc = state(guard);
  assert(doc["supported"] == true);
  assert(doc["recovery"]["status"] == expected);
  assert(doc["recovery"]["active"] == active);
}
void stable(CrashGuard& guard) {
  fake::now = 10 * 60 * 1000;
  guard.onPeriodic();
}
void seedCrash(int slot = 0, int strike = 9, uint32_t lastSwitch = 0,
               uint32_t uptime = 5000) {
  NvsState score{};
  score.version = 1; score.bootCount = 10; score.lastSwitchBoot = lastSwitch;
  score.slot[slot].strike = strike;
  assert(esp32m::debug::nvsWrite(score));
  rtcReset(); s_rtc.bootCount = 10; s_rtc.slot = slot;
  s_rtc.inProgress = 1; s_rtc.checkpoint = (uint8_t)CrashGuard::Checkpoint::Inited;
  s_rtc.lastUptimeMs = uptime;
  fake::actions.clear();
}
void trigger(int slot = 0) {
  bool restarted = false;
  try { boot(slot, ESP_RST_PANIC); } catch (const fake::Restart&) { restarted = true; }
  assert(restarted);
}
std::unique_ptr<CrashGuard> recovery() {
  seedCrash(); trigger();
  return boot(1);
}

void testAutomaticAndCompact() {
  reset(); seedCrash(); trigger();
  auto intent = std::find(fake::actions.begin(), fake::actions.end(), "commit:recovery_intent");
  auto select = std::find(fake::actions.begin(), fake::actions.end(), "select");
  assert(intent < select);
  assert(fake::selected == 1 && s_rtc.plannedReset && !s_rtc.inProgress);
  assert(s_rtc.slot == 1 && !strcmp(s_rtc.lastCrashSig, "switch"));
  assert(fake::nvs.count("recovery") == 0);
  auto guard = boot(1);
  status(*guard, "fallback", true);
  auto doc = state(*guard);
  assert(doc["recovery"]["boot"] == 11);
  assert(doc["recovery"]["resetReason"] == (int)ESP_RST_PANIC);
  assert(doc["recovery"]["checkpoint"] == 20);
  assert(doc["recovery"]["uptime"] == 5000);
  assert(doc["recovery"]["strike"] == 14);
  assert(doc["recovery"]["early"] == 1);
  assert(doc["recovery"]["from"]["slot"] == 0 && doc["recovery"]["to"]["slot"] == 1);
  assert(doc["recovery"]["from"]["sha256"] != doc["recovery"]["to"]["sha256"]);
  assert(doc["current"]["slot"] == 1 && doc["recovery"]["reason"] == "unexpected-reset");
  assert(!doc["ota"].is<JsonArray>() && !doc["rtc"].is<JsonObject>());
  const auto reads = fake::descriptors;
  const auto events = fake::stateEvents;
  std::string firstSnapshot;
  serializeJson(doc, firstSnapshot);
  for (unsigned i = 0; i < 10; ++i) { (void)state(*guard); fake::now += 1000; guard->onPeriodic(); }
  assert(fake::descriptors == reads && fake::stateEvents == events);
  std::string laterSnapshot;
  serializeJson(state(*guard), laterSnapshot);
  assert(firstSnapshot == laterSnapshot);
  auto full = state(*guard, false);
  assert(full["ota"].is<JsonArray>() && full["recovery"]["status"] == "fallback");
  esp32m::RequestContext ctx{};
  std::unique_ptr<JsonDocument> info(guard->getInfo(ctx));
  assert((*info)["recovery"]["status"] == "fallback");
  std::cout << "automatic decision, causal metadata, compact subscription: passed\n";
}

void testLifecycleAndIdentity() {
  reset(); auto guard = recovery();
  stable(*guard); status(*guard, "fallback", true);
  guard = boot(1, ESP_RST_POWERON, true); status(*guard, "fallback", true);
  // Failed upload or merely overwriting the inactive slot leaves the warning.
  fake::version(0, "same-version", 0xcc);
  status(*guard, "fallback", true);
  guard = boot(1); status(*guard, "fallback", true);
  guard = boot(0); status(*guard, "testing", true);
  assert(state(*guard)["current"]["sha256"] != state(*guard)["recovery"]["from"]["sha256"]);
  fake::now = 599999; guard->onPeriodic(); status(*guard, "testing", true);
  const auto events = fake::stateEvents;
  stable(*guard); status(*guard, "resolved", false);
  assert(fake::stateEvents == events + 1);
  guard->onPeriodic(); assert(fake::stateEvents == events + 1);
  guard = boot(1, ESP_RST_POWERON, true); status(*guard, "resolved", false);
  // Retrying the exact rejected build at the source is also an evaluation.
  reset(); guard = recovery(); guard = boot(0); status(*guard, "testing", true);
  stable(*guard); status(*guard, "resolved", false);
  guard = boot(1); status(*guard, "resolved", false);
  std::cout << "power cycles, failed uploads, equal-version replacement, same-SHA retry, resolution: passed\n";
}

void testManualAndOrdinaryResets() {
  reset(); auto guard = boot(0, ESP_RST_POWERON, true);
  assert(state(*guard)["recovery"].isNull());
  esp32m::Request req{"switchOta"};
  assert(guard->handleRequest(req));
  assert(req.response == ESP_OK && fake::appRestarts == 1);
  guard = boot(1); assert(state(*guard)["recovery"].isNull());
  guard = boot(1, ESP_RST_BROWNOUT); assert(state(*guard)["recovery"].isNull());
  // A historical resolved incident cannot be reactivated by a manual switch.
  reset(); guard = recovery(); guard = boot(0); stable(*guard);
  guard->handleRequest(req); guard = boot(1); status(*guard, "resolved", false);
  std::cout << "manual switches, brownouts, power-on, historical incident: passed\n";
}

void testFailedSelectionAndPriorIncident() {
  reset(); seedCrash(); fake::setBootResult = ESP_FAIL;
  auto guard = boot(0, ESP_RST_PANIC); status(*guard, "inactive", false);
  assert(fake::selected == 0);
  fake::setBootResult = ESP_OK;
  esp32m::Request req{"switchOta"}; guard->handleRequest(req);
  guard = boot(1); assert(state(*guard)["recovery"].isNull());
  // Reverse recovery must preserve the original confirmed event if selection fails.
  reset(); guard = recovery();
  const auto saved = fake::nvs["recovery"];
  seedCrash(1); fake::setBootResult = ESP_FAIL;
  guard = boot(1, ESP_RST_PANIC); status(*guard, "fallback", true);
  assert(fake::nvs["recovery"] == saved);
  fake::setBootResult = ESP_OK;
  guard = boot(1, ESP_RST_POWERON, true); status(*guard, "fallback", true);
  // A stale selected intent whose target never booted cannot be confirmed by
  // a later explicit manual switch, even if that cancellation write fails.
  reset(); seedCrash(); trigger(); guard = boot(0, ESP_RST_POWERON, true);
  status(*guard, "pending", false);
  fake::failCommit["recovery_intent"] = 1;
  guard->handleRequest(req);
  guard->onDone(esp32m::DoneReason::Restart);  // App::restart EventDone
  guard = boot(1); assert(state(*guard)["recovery"].isNull());
  guard = boot(1, ESP_RST_POWERON, true); assert(state(*guard)["recovery"].isNull());
  std::cout << "failed selection and failed reverse recovery preserve previous incident: passed\n";
}

void testStorageFailures() {
  // Complete telemetry failure never changes selection/restart behavior.
  reset(); seedCrash(); fake::failSet["recovery_intent"] = -1;
  fake::failSet["recovery"] = -1; trigger();
  assert(fake::selected == 1);
  auto guard = boot(1); assert(state(*guard)["recovery"].isNull());
  // A failed success-marker write reconciles using unchanged score ledger.
  reset(); seedCrash(); unsigned commits = 0;
  fake::afterCommit = [&](const std::string& key) {
    if (key == "recovery_intent" && ++commits == 1) fake::failCommit[key] = 1;
  };
  trigger(); fake::afterCommit = {}; guard = boot(1, ESP_RST_POWERON, true);
  status(*guard, "fallback", true);
  // Failed confirmation retains staged intent and retries next boot.
  reset(); seedCrash(); trigger(); fake::failCommit["recovery"] = 1;
  guard = boot(1); status(*guard, "fallback", true);
  assert(fake::nvs.count("recovery") == 0);
  guard = boot(1, ESP_RST_POWERON, true); status(*guard, "fallback", true);
  assert(fake::nvs.count("recovery") == 1);
  // A failed cancellation after promotion cannot reopen a resolved event.
  reset(); seedCrash(); trigger(); fake::failSet["recovery_intent"] = -1;
  guard = boot(1); status(*guard, "fallback", true);
  guard = boot(0); status(*guard, "testing", true); stable(*guard);
  guard = boot(1, ESP_RST_POWERON, true); status(*guard, "resolved", false);
  // A failed acceptance write keeps warning active and durable.
  reset(); guard = recovery(); guard = boot(0); fake::failCommit["recovery"] = 1;
  stable(*guard); status(*guard, "testing", true);
  guard = boot(0, ESP_RST_POWERON, true); stable(*guard); status(*guard, "resolved", false);
  std::cout << "intent, marker, promotion, cancellation and acceptance storage failures: passed\n";
}

void testPowerCuts() {
  // Durable intent before selection alone is not a completed recovery.
  reset(); seedCrash(); fake::afterCommit = [](const std::string& key) {
    if (key == "recovery_intent") throw fake::PowerCut{};
  };
  try { boot(0, ESP_RST_PANIC); assert(false); } catch (const fake::PowerCut&) {}
  fake::afterCommit = {}; auto guard = boot(0, ESP_RST_POWERON, true);
  assert(state(*guard)["recovery"].isNull());
  // Power loss after selecting but before any success evidence stays pending.
  reset(); seedCrash(); fake::afterSetBoot = [] { throw fake::PowerCut{}; };
  try { boot(0, ESP_RST_PANIC); assert(false); } catch (const fake::PowerCut&) {}
  fake::afterSetBoot = {}; guard = boot(1, ESP_RST_POWERON, true);
  status(*guard, "pending", false);
  // Once the success marker commits, power loss before v1/RTC writes is safe.
  reset(); seedCrash(); unsigned commits = 0;
  fake::afterCommit = [&](const std::string& key) {
    if (key == "recovery_intent" && ++commits == 2) throw fake::PowerCut{};
  };
  try { boot(0, ESP_RST_PANIC); assert(false); } catch (const fake::PowerCut&) {}
  fake::afterCommit = {}; guard = boot(1, ESP_RST_POWERON, true);
  status(*guard, "fallback", true);
  std::cout << "power cuts before selection, after selection, after success marker: passed\n";
}

void testBehaviorAndInvalidData() {
  reset(); seedCrash(0, 9, 10); auto guard = boot(0, ESP_RST_PANIC);
  assert(fake::selected == 0 && state(*guard)["recovery"].isNull());
  reset(); seedCrash(); s_rtc.plannedReset = 1;
  guard = boot(0, ESP_RST_PANIC);
  assert(fake::selected == 0 && guard->_state.slot0.strike == 9 &&
         guard->_state.slot0.crashes == 0 && state(*guard)["recovery"].isNull());
  reset(); seedCrash(); guard = boot(0, ESP_RST_BROWNOUT);
  assert(fake::selected == 0 && guard->_state.slot0.strike == 9 &&
         guard->_state.slot0.brownouts == 1 && state(*guard)["recovery"].isNull());
  // Exercise the consecutive-early branch independently of the score limit.
  reset(); seedCrash(0, 0, 0, 30000);
  NvsState early{}; assert(esp32m::debug::nvsRead(early));
  early.slot[0].consecutiveEarly = 2; assert(esp32m::debug::nvsWrite(early));
  trigger(); guard = boot(1); status(*guard, "fallback", true);
  assert(state(*guard)["recovery"]["early"] == 3 &&
         state(*guard)["recovery"]["strike"] == 3);
  reset(); seedCrash(0, 0, 0, 600000); guard = boot(0, ESP_RST_PANIC);
  assert(fake::selected == 0 && guard->_state.slot0.strike == 1);
  reset(); seedCrash(); fake::noOther = true; guard = boot(0, ESP_RST_PANIC);
  assert(fake::nvs.count("recovery_intent") == 0);
  reset(); guard = recovery();
  // Legacy score layout/key are unchanged and sufficient without new records.
  assert(fake::nvs["state"].size() == sizeof(NvsState));
  auto valid = fake::nvs["recovery"];
  for (int kind = 0; kind < 7; ++kind) {
    auto record = *reinterpret_cast<const CrashGuard::RecoveryRecord*>(valid.data());
    if (kind == 0) record.version = 99;
    if (kind == 1) record.phase = static_cast<CrashGuard::RecoveryPhase>(99);
    if (kind == 2) record.to.slot = record.from.slot;
    if (kind == 3) record.to.ver[31] = 'x';
    if (kind == 4) std::memset(record.to.sha256, 0, 32);
    if (kind == 5) record.boot = 0;
    if (kind == 6) record.selectionSucceeded = 5;
    fake::nvs["recovery"] = std::vector<uint8_t>(
        reinterpret_cast<uint8_t*>(&record), reinterpret_cast<uint8_t*>(&record) + sizeof(record));
    guard = boot(1, ESP_RST_POWERON, true); assert(state(*guard)["recovery"].isNull());
  }
  fake::nvs["recovery"] = std::vector<uint8_t>(3, 0xff);
  guard = boot(1); assert(state(*guard)["recovery"].isNull());
  std::cout << "existing anti-ping-pong, scoring, absent destination, v1 layout, invalid records: passed\n";
}

int main() {
  testAutomaticAndCompact();
  testLifecycleAndIdentity();
  testManualAndOrdinaryResets();
  testFailedSelectionAndPriorIncident();
  testStorageFailures();
  testPowerCuts();
  testBehaviorAndInvalidData();
  std::cout << "CrashGuard production host scenarios passed\n";
}
