#include "esp32m/net/wifi/roaming.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

static unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) \
  throw std::runtime_error("check failed at line " + std::to_string(__LINE__) + ": " #condition); } while (0)
constexpr int ESP_OK = 0, ESP_FAIL = -1, WIFI_IF_STA = 0;
struct wifi_ap_record_t { uint8_t ssid[33] = {}, bssid[6] = {}; };
struct wifi_config_t {
  struct { uint8_t ssid[32] = {}, password[64] = {}; int savedField = 1234; } sta;
};
namespace fake {
  wifi_config_t config;
  bool ota = false, succeeds = true, manualDuringAttempt = false;
  int gets = 0, sets = 0, disconnects = 0, waits = 0, result = ESP_OK;
  void reset(const std::string& password = "authenticated-password") {
    config = {};
    std::memcpy(config.sta.ssid, "MOON", 4);
    std::memcpy(config.sta.password, password.data(), password.size());
    ota = manualDuringAttempt = false;
    succeeds = true;
    gets = sets = disconnects = waits = 0;
    result = ESP_OK;
  }
}
uint32_t millis() { return 70000; }
int esp_wifi_get_config(int, wifi_config_t* config) { ++fake::gets; *config = fake::config; return fake::result; }
int esp_wifi_set_config(int, wifi_config_t* config) { ++fake::sets; fake::config = *config; return ESP_OK; }
int esp_wifi_disconnect() { ++fake::disconnects; return ESP_OK; }
template<class Predicate> bool waitState(Predicate predicate, unsigned timeout) {
  ++fake::waits;
  CHECK(timeout == 1000);
  return predicate();
}
namespace esp32m::net {
  namespace ota { bool isRunning() { return fake::ota; } }
  enum class StaStatus { Initial, Connected };
  struct Sta {
    StaStatus status = StaStatus::Connected;
    int disconnects = 0;
    void disconnect() { ++disconnects; }
    bool isConnected() const { return false; }
    void setStatus(StaStatus value) { status = value; }
  };
  struct ApInfo {
    int id;
    std::string ssid, password;
    std::array<uint8_t, 6> bssid;
    ApInfo(int idValue, const char* ssidValue, const char* passValue,
           const uint8_t* bssidValue) : id(idValue), ssid(ssidValue), password(passValue) {
      std::memcpy(bssid.data(), bssidValue, 6);
    }
  };
  struct Wifi {
    bool _managedExternally = false, _stopped = false;
    std::unique_ptr<int> _connect;
    wifi::RoamingPolicy _roaming;
    Sta _sta;
    std::unique_ptr<ApInfo> attempted;
    bool ignoredPin = true, remembered = true;
    bool roamTo(const wifi_ap_record_t& target);
    bool tryConnect(ApInfo* candidate, bool noBssid, bool remember) {
      attempted = std::make_unique<ApInfo>(*candidate);
      ignoredPin = noBssid;
      remembered = remember;
      // Simulate the driver having selected a temporary profile.
      fake::config.sta.savedField = 999;
      if (fake::manualDuringAttempt) _connect = std::make_unique<int>(1);
      return fake::succeeds;
    }
  };
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expression) (expression)
#include "migration.inc"
}

int main() {
  using esp32m::net::Wifi;
  wifi_ap_record_t target;
  std::memcpy(target.ssid, "MOON", 4);
  target.bssid[5] = 2;
  for (const std::string& password : {std::string("authenticated-password"), std::string(64, 'a')}) {
    fake::reset(password);
    Wifi wifi;
    CHECK(wifi.roamTo(target));
    CHECK(wifi.attempted && wifi.attempted->ssid == "MOON");
    CHECK(wifi.attempted->password == password);
    CHECK(wifi.attempted->id == 0 && wifi.attempted->bssid[5] == 2);
    CHECK(!wifi.ignoredPin && !wifi.remembered);
    CHECK(fake::sets == 0 && fake::disconnects == 0);
  }
  fake::reset(); Wifi failed;
  fake::succeeds = false;
  failed._roaming.disconnected(true, 60000);
  CHECK(!failed.roamTo(target));
  CHECK(fake::sets == 1 && fake::disconnects == 1 && fake::waits == 1);
  CHECK(fake::config.sta.savedField == 1234);
  CHECK(failed._sta.status == esp32m::net::StaStatus::Initial);
  CHECK(!failed._roaming.associationPending(millis()));

  // A manual request queued just as a migration succeeds still wins.
  fake::reset(); Wifi raced;
  fake::manualDuringAttempt = true;
  CHECK(!raced.roamTo(target));
  CHECK(raced._connect && raced._sta.disconnects == 1);
  CHECK(fake::sets == 1 && fake::config.sta.savedField == 1234);
  CHECK(raced._sta.status == esp32m::net::StaStatus::Initial);

  for (int guard = 0; guard < 6; ++guard) {
    fake::reset(); Wifi blocked;
    if (guard == 0) blocked._managedExternally = true;
    if (guard == 1) blocked._stopped = true;
    if (guard == 2) blocked._connect = std::make_unique<int>(1);
    if (guard == 3) fake::ota = true;
    if (guard == 4) fake::result = ESP_FAIL;
    if (guard == 5) std::memcpy(fake::config.sta.ssid, "OTHER", 5);
    CHECK(!blocked.roamTo(target));
    CHECK(!blocked.attempted && fake::sets == 0 && fake::disconnects == 0);
  }
  std::cout << "PASS production Wi-Fi migration credentials/restoration: "
            << checks << " checks\n";
}
