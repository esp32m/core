#include "esp32m/net/wifi/roaming.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

static unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) \
  throw std::runtime_error("check failed at line " + std::to_string(__LINE__) + ": " #condition); } while (0)

constexpr int ESP_OK = 0, ESP_FAIL = -1, WIFI_IF_STA = 0;
constexpr int WIFI_SCAN_TYPE_ACTIVE = 0, REASON_FRAME_LOSS = 6;
enum {
  WIFI_AUTH_OPEN, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK,
  WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_WPA2_ENTERPRISE, WIFI_AUTH_WPA3_PSK,
  WIFI_AUTH_WPA2_WPA3_PSK,
};
struct wifi_ap_record_t {
  uint8_t ssid[33] = {}, bssid[6] = {};
  int8_t rssi = -88;
  int authmode = WIFI_AUTH_WPA2_PSK;
};
struct wifi_config_t {
  struct {
    uint8_t ssid[33] = {};
    struct { int authmode = WIFI_AUTH_WPA2_PSK; } threshold;
  } sta;
};
struct wifi_scan_config_t {
  uint8_t* ssid = nullptr;
  int scan_type = 0;
  struct { struct { unsigned min = 0, max = 0; } active; } scan_time;
};
struct EventGroup { uint32_t bits = 0; };
uint32_t xEventGroupClearBits(EventGroup* group, uint32_t bits) {
  const auto before = group->bits;
  group->bits &= ~bits;
  return before;
}
void xEventGroupSetBits(EventGroup* group, uint32_t bits) { group->bits |= bits; }

namespace fake {
  uint32_t now = 0;
  bool ota = false, oom = false;
  int rearm = 0, threshold = 0, neighbors = 0, btm = 0;
  int started = 0, stopped = 0, cleared = 0, recordsRead = 0;
  int neighborResult = ESP_OK, btmResult = ESP_OK, scanResult = ESP_OK;
  int countResult = ESP_OK, recordResult = ESP_OK, infoResult = ESP_OK;
  wifi_ap_record_t current;
  wifi_config_t config;
  std::vector<wifi_ap_record_t> records;
  void reset() {
    now = 0;
    ota = oom = false;
    rearm = threshold = neighbors = btm = started = stopped = cleared = recordsRead = 0;
    neighborResult = btmResult = scanResult = countResult = recordResult = infoResult = ESP_OK;
    current = {};
    std::strcpy(reinterpret_cast<char*>(current.ssid), "MOON");
    current.bssid[5] = 1;
    config = {};
    std::strcpy(reinterpret_cast<char*>(config.sta.ssid), "MOON");
    records.clear();
  }
}

uint32_t millis() { return fake::now; }
int esp_wifi_set_rssi_threshold(int value) { ++fake::rearm; fake::threshold = value; return ESP_OK; }
int esp_rrm_send_neighbor_report_request() { ++fake::neighbors; return fake::neighborResult; }
int esp_wnm_send_bss_transition_mgmt_query(int, const char*, uint8_t) { ++fake::btm; return fake::btmResult; }
int esp_wifi_scan_start(wifi_scan_config_t* config, bool blocking) {
  CHECK(!blocking);
  CHECK(config->ssid && !std::strcmp(reinterpret_cast<const char*>(config->ssid), "MOON"));
  ++fake::started;
  return fake::scanResult;
}
int esp_wifi_scan_stop() { ++fake::stopped; return ESP_OK; }
int esp_wifi_clear_ap_list() { ++fake::cleared; return ESP_OK; }
int esp_wifi_sta_get_ap_info(wifi_ap_record_t* info) { *info = fake::current; return fake::infoResult; }
int esp_wifi_get_config(int, wifi_config_t* config) { *config = fake::config; return ESP_OK; }
int esp_wifi_scan_get_ap_num(uint16_t* count) {
  *count = static_cast<uint16_t>(fake::records.size());
  return fake::countResult;
}
int esp_wifi_scan_get_ap_records(uint16_t* count, wifi_ap_record_t* records) {
  ++fake::recordsRead;
  *count = std::min<uint16_t>(*count, static_cast<uint16_t>(fake::records.size()));
  std::copy_n(fake::records.begin(), *count, records);
  return fake::recordResult;
}
void* fakeCalloc(size_t count, size_t size) { return fake::oom ? nullptr : std::calloc(count, size); }

namespace esp32m::net {
  enum WifiFlags {
    StaConnected = 1 << 2, StaGotIp = 1 << 3,
    Scanning = 1 << 5, ScanDone = 1 << 6, RoamConnected = 1 << 7,
    RoamDisconnected = 1 << 8, RoamAssociation = 1 << 9, RoamNeighbor = 1 << 10,
  };
  namespace ota { bool isRunning() { return fake::ota; } }
  struct FakeAp { int clients = 0; int clientsCount() const { return clients; } };
  struct Wifi {
    EventGroup group;
    EventGroup* _eventGroup = &group;
    bool _managedExternally = false, _stopped = false, _roamScan = false;
    bool connected = true;
    std::unique_ptr<int> _connect;
    std::unique_ptr<FakeAp> _ap;
    void* _pendingResponse = nullptr;
    int8_t _rssiThreshold = -75;
    uint32_t _roamScanStarted = 0;
    uint8_t _roamScanSsid[33] = {};
    wifi_scan_config_t _roamScanConfig = {};
    wifi::RoamingPolicy _roaming;
    std::vector<wifi_ap_record_t> migrations;
    bool isConnected() const { return connected; }
    void checkRoaming();
    void finishRoamingScan();
    bool roamTo(const wifi_ap_record_t& target) { migrations.push_back(target); return true; }
  };
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expression) (expression)
#define logI(...) ((void)0)
#define MACSTR "%u:%u:%u:%u:%u:%u"
#define MAC2STR(mac) (mac)[0], (mac)[1], (mac)[2], (mac)[3], (mac)[4], (mac)[5]
#define calloc fakeCalloc
#include "worker.inc"
#undef calloc
}

using esp32m::net::Wifi;
using namespace esp32m::net;
using Policy = esp32m::net::wifi::RoamingPolicy;

static void step(Wifi& wifi, uint32_t now) { fake::now = now; wifi.checkRoaming(); }
static void requestAndReachScan(Wifi& wifi) {
  fake::neighborResult = fake::btmResult = ESP_FAIL;
  step(wifi, Policy::AttemptInterval);
  step(wifi, Policy::AttemptInterval + 1);
  step(wifi, Policy::AttemptInterval + 2);
}
static wifi_ap_record_t candidate(uint8_t address, int rssi, const char* ssid = "MOON",
                                  int auth = WIFI_AUTH_WPA2_PSK) {
  wifi_ap_record_t record;
  std::strcpy(reinterpret_cast<char*>(record.ssid), ssid);
  record.bssid[5] = address;
  record.rssi = static_cast<int8_t>(rssi);
  record.authmode = auth;
  return record;
}

static void rearmAndReports() {
  fake::reset(); Wifi wifi;
  wifi.group.bits = RoamConnected;
  step(wifi, 0);
  step(wifi, Policy::AttemptInterval - 1);
  CHECK(fake::rearm == 0 && fake::neighbors == 0);
  step(wifi, Policy::AttemptInterval);
  CHECK(fake::rearm == 1 && fake::threshold == -75 && fake::neighbors == 1);
  step(wifi, Policy::AttemptInterval + 1);
  CHECK(fake::rearm == 1 && fake::btm == 0);
  wifi.group.bits |= RoamNeighbor; // includes successful/empty/null event payloads
  step(wifi, Policy::AttemptInterval + 2);
  CHECK(fake::btm == 1);
  CHECK(!(wifi.group.bits & RoamNeighbor));
  step(wifi, Policy::AttemptInterval + 2 + Policy::TransitionTimeout - 1);
  CHECK(fake::started == 0);
  step(wifi, Policy::AttemptInterval + 2 + Policy::TransitionTimeout);
  CHECK(fake::started == 1 && wifi._roamScan);
  CHECK((wifi.group.bits & Scanning) != 0);
  wifi.group.bits |= ScanDone;
  step(wifi, fake::now + 1);
  CHECK(!wifi._roamScan && fake::cleared == 1);
  const auto completed = fake::now;
  step(wifi, completed + Policy::AttemptInterval - 1);
  CHECK(fake::rearm == 1);
  step(wifi, completed + Policy::AttemptInterval);
  CHECK(fake::rearm == 2 && fake::neighbors == 2);
}

static void permissionsAndScanOwnership() {
  for (int guard = 0; guard < 6; ++guard) {
    fake::reset(); Wifi wifi;
    switch (guard) {
      case 0: wifi._managedExternally = true; break;
      case 1: wifi._stopped = true; break;
      case 2: wifi._connect = std::make_unique<int>(1); break;
      case 3: fake::ota = true; break;
      case 4: wifi._rssiThreshold = 0; break;
      case 5: wifi.connected = false; break;
    }
    step(wifi, Policy::AttemptInterval);
    CHECK(fake::neighbors == 0 && fake::rearm == 0 && fake::btm == 0 && fake::started == 0);
  }
  fake::reset(); Wifi healthy;
  fake::current.rssi = -65;
  step(healthy, Policy::AttemptInterval);
  CHECK(fake::neighbors == 0 && fake::rearm == 0);

  for (int owner = 0; owner < 3; ++owner) {
    fake::reset(); Wifi wifi;
    if (owner == 0) wifi._pendingResponse = &wifi;
    if (owner == 1) wifi.group.bits |= Scanning;
    if (owner == 2) {
      wifi._ap = std::make_unique<FakeAp>();
      wifi._ap->clients = 1;
    }
    requestAndReachScan(wifi);
    CHECK(fake::started == 0 && fake::stopped == 0 && fake::cleared == 0);
    wifi._pendingResponse = nullptr;
    wifi.group.bits &= ~Scanning;
    wifi._ap.reset();
    step(wifi, Policy::AttemptInterval + 3);
    if (owner == 2) {
      // Captive clients suppress all roaming work until they leave, while
      // an existing UI scan only postpones the background survey itself.
      CHECK(!wifi._roamScan && fake::neighbors == 1);
      step(wifi, Policy::AttemptInterval + 4);
      step(wifi, Policy::AttemptInterval + 5);
    }
    CHECK(fake::started == 1 && wifi._roamScan);
  }

  // Cancelling our own scan is allowed; it must never leave stale result bits.
  for (int reason = 0; reason < 3; ++reason) {
    fake::reset(); Wifi wifi;
    requestAndReachScan(wifi);
    CHECK(wifi._roamScan);
    if (reason == 0) fake::ota = true;
    if (reason == 1) wifi._connect = std::make_unique<int>(1);
    step(wifi, Policy::AttemptInterval + 2 + (reason == 2 ? 15000 : 1));
    CHECK(!wifi._roamScan && fake::stopped == 1 && fake::cleared == 1);
    CHECK((wifi.group.bits & (Scanning | ScanDone)) == 0);
  }
}

static void layerTwoWithoutIp() {
  fake::reset(); Wifi wifi;
  wifi.connected = false;
  wifi.group.bits = RoamConnected;
  step(wifi, 123);
  CHECK(wifi._roaming.associationPending(123));
  CHECK(wifi._roaming.associationPending(123 + Policy::AssociationTimeout - 1));
  CHECK(!wifi._roaming.associationPending(123 + Policy::AssociationTimeout));
  wifi.group.bits = RoamDisconnected | RoamAssociation;
  step(wifi, 20000);
  CHECK(wifi._roaming.associationPending(20000));
  // An ordinary disconnection clears the grace even if roaming is disabled.
  wifi._rssiThreshold = 0;
  wifi.group.bits = RoamDisconnected;
  step(wifi, 20001);
  CHECK(!wifi._roaming.associationPending(20001));
  CHECK(fake::neighbors == 0 && fake::btm == 0 && fake::started == 0);
}

static void filterAndFailure() {
  fake::reset(); Wifi wifi;
  fake::records = {
    candidate(9, -30, "OTHER"), candidate(8, -35, "MOON", WIFI_AUTH_WPA2_ENTERPRISE),
    candidate(1, -40), candidate(7, -60, "MOON", WIFI_AUTH_OPEN),
    candidate(2, -76), candidate(3, -65), candidate(4, -50),
  };
  requestAndReachScan(wifi);
  wifi.group.bits |= ScanDone;
  step(wifi, Policy::AttemptInterval + 3);
  CHECK(fake::recordsRead == 1 && wifi.migrations.size() == 1);
  CHECK(wifi.migrations[0].bssid[5] == 4 && wifi.migrations[0].rssi == -50);

  // A manual scan request arriving during our survey keeps its own result path.
  fake::reset(); Wifi manual;
  fake::records = {candidate(4, -50)};
  requestAndReachScan(manual);
  manual._pendingResponse = &manual;
  manual.group.bits |= ScanDone;
  step(manual, Policy::AttemptInterval + 3);
  CHECK(manual.migrations.empty());
  CHECK(manual._pendingResponse == &manual && !manual._roamScan);

  for (int failure = 0; failure < 4; ++failure) {
    fake::reset(); Wifi failed;
    fake::records = {candidate(4, -50)};
    requestAndReachScan(failed);
    if (failure == 0) fake::oom = true;
    if (failure == 1) fake::countResult = ESP_FAIL;
    if (failure == 2) fake::recordResult = ESP_FAIL;
    if (failure == 3) fake::infoResult = ESP_FAIL;
    failed.group.bits |= ScanDone;
    step(failed, Policy::AttemptInterval + 3);
    CHECK(failed.migrations.empty() && !failed._roamScan);
    if (failure <= 2) CHECK(fake::cleared == 1);
  }
}

int main() {
  rearmAndReports();
  permissionsAndScanOwnership();
  layerTwoWithoutIp();
  filterAndFailure();
  std::cout << "PASS production Wi-Fi roaming worker/scan integration: "
            << checks << " checks\n";
}
