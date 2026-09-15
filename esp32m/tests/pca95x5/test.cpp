#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_TIMEOUT = 0x107;
constexpr int ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_STATE = 0x103;
constexpr int ESP_ERR_INVALID_RESPONSE = 0x108, BIT0 = 1, BIT1 = 2;
#define ESP_CHECK_RETURN(x) do { const auto err = (x); if (err != ESP_OK) return err; } while (false)
inline esp_err_t logError(esp_err_t error) { return error; }
#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) logError(x)
#define logW(...) ((void)0)

namespace esp32m {
enum class Endian { Little, Big };
namespace i2c {
class MasterDev {
 public:
  struct Write { uint8_t reg; uint16_t value; };
  std::array<uint8_t, 8> registers = {255, 255, 255, 255, 0, 0, 255, 255};
  std::array<int, 8> reads{};
  std::vector<Write> writes;
  int failRead = -1, failWrite = -1;
  bool acceptFailedWrite = false, retryInvalidResponse = false;
  esp_err_t failure = ESP_ERR_TIMEOUT;
  void setEndianness(Endian) {}
  void setRetryInvalidResponse(bool value) { retryInvalidResponse = value; }
  uint16_t get(uint8_t reg) const { return registers[reg] | uint16_t(registers[reg + 1]) << 8; }
  void put(uint8_t reg, uint16_t value) { registers[reg] = value; registers[reg + 1] = value >> 8; }
  template<class T> esp_err_t read(uint8_t reg, T& value) {
    ++reads.at(reg);
    if (failRead == reg) { failRead = -1; return failure; }
    value = sizeof(T) == 1 ? registers.at(reg) : get(reg);
    return ESP_OK;
  }
  template<class T> esp_err_t write(uint8_t reg, T value) {
    const bool fail = failWrite == reg;
    writes.push_back({reg, uint16_t(value)});
    if (!fail || acceptFailedWrite) {
      registers.at(reg) = value;
      if (sizeof(T) > 1) registers.at(reg + 1) = value >> 8;
    }
    if (fail) { failWrite = -1; return failure; }
    return ESP_OK;
  }
  static MasterDev* create(uint8_t) { return new MasterDev; }
};
}
namespace io {
class IPin {};
class IPins {
 public:
  virtual const char* name() const = 0;
  int count() const { return _count; }
 protected:
  void init(int count) { _count = count; }
  virtual IPin* newPin(int) = 0;
 private:
  int _count = 0;
};
namespace pin {
enum class Flags { Input = 1, Output = 2, PullUp = 4, OpenDrain = 8 };
Flags operator|(Flags a, Flags b) { return Flags(int(a) | int(b)); }
Flags& operator|=(Flags& a, Flags b) { return a = a | b; }
}
}
}
#include "production.inc"

using namespace esp32m;
using io::pca95x5::Register;
using io::pin::Tx;
void require(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
struct Fixture {
  i2c::MasterDev* bus = new i2c::MasterDev;
  io::Pca95x5 pca{bus};
  Fixture() { pca.setRecoveryOutput(0x4300, 0); }
  void configure() {
    Tx tx(Tx::Write);
    require(pca.writePin(8, false) == ESP_OK, "latch 8 Off");
    require(pca.writePin(9, false) == ESP_OK, "latch 9 Off");
    require(pca.writePin(14, false) == ESP_OK, "fan Off");
    require(tx.commit() == ESP_OK, "safe latch commit");
    for (int pin : {8, 9, 14}) require(pca.setPinMode(pin, false) == ESP_OK, "configure direction");
    require(bus->get(6) == 0xbcff, "greenhouse directions");
  }
};

void resetRecovery() {
  Fixture f; f.configure();
  require(f.pca.writePin(9, true) == ESP_OK, "motor running");
  f.bus->put(2, 0xffff); f.bus->put(6, 0xffff); f.bus->put(4, 0);
  f.bus->writes.clear();
  bool value = true;
  require(f.pca.readPin(9, value) == ESP_ERR_INVALID_STATE, "interrupted operation reports reset");
  require(f.bus->get(6) == 0xbcff && (f.bus->get(2) & 0x4300) == 0, "recover to safe motor/fan state");
  require(f.bus->writes.size() == 3 && f.bus->writes[0].reg == 2 && f.bus->writes[2].reg == 6,
          "safe output before direction restoration");
  require(f.pca.readPin(9, value) == ESP_OK && !value, "verified recovered output");
  require(f.pca.writePin(14, true) == ESP_OK && (f.bus->get(2) & 0x300) == 0, "no stale motor replay on fan command");
}

void failedModeAndInitialization() {
  auto* bus = new i2c::MasterDev;
  bus->failRead = 2;
  io::Pca95x5 pca(bus);
  pca.setRecoveryOutput(0x4300, 0);
  require(bus->retryInvalidResponse, "PCA enables safe invalid-response retries");
  require(pca.writePin(9, false) == ESP_OK, "retry failed constructor initialization");
  bus->failWrite = 6;
  require(pca.setPinMode(9, false) == ESP_ERR_TIMEOUT, "direction error propagated");
  bus->put(0, 0);
  bool value = true;
  require(pca.readPin(9, value) == ESP_ERR_INVALID_STATE, "uncertain write forces recovery");
  require(pca.readPin(9, value) == ESP_OK && !value && bus->get(6) == 0xffff,
          "failed mode not published as confirmed output");
  io::Pca95x5 absent(nullptr);
  require(absent.writePin(0, false) == ESP_ERR_INVALID_STATE, "null device fails without crash");
}

void stagingAndFailedCommit() {
  Fixture f; f.configure();
  {
    Tx tx(Tx::Write);
    require(f.pca.writePin(8, true) == ESP_OK, "stage one motor pin");
    require(f.pca.writePin(16, true) == ESP_ERR_INVALID_ARG, "reject invalid second pin");
    tx.abort();
  }
  require(f.pca.writePin(14, true) == ESP_OK && (f.bus->get(2) & 0x300) == 0, "abort discards partial motor batch");
  {
    Tx tx(Tx::Write);
    require(f.pca.writePin(9, true) == ESP_OK, "stage motion");
    f.bus->failWrite = 2;
    f.bus->acceptFailedWrite = true;
    require(tx.commit() == ESP_ERR_TIMEOUT, "post-accept output failure propagated");
  }
  require((f.bus->get(2) & 0x200) != 0, "mock failure may have changed hardware");
  require(f.pca.writePin(8, false) == ESP_ERR_INVALID_STATE, "next access restores safe state and rejects stale command");
  require((f.bus->get(2) & 0x4300) == 0, "accepted-but-failed write recovered Off");
  require(f.pca.writePin(14, true) == ESP_OK && (f.bus->get(2) & 0x300) == 0, "failed command cannot leak later");
  {
    Tx tx(Tx::Write);
    require(f.pca.writePin(9, true) == ESP_OK, "stage before verification failure");
    f.bus->failRead = 6;
    require(tx.commit() == ESP_ERR_TIMEOUT, "commit verification failure returned");
  }
  require(f.pca.writePin(14, false) == ESP_OK && (f.bus->get(2) & 0x300) == 0, "verification failure drops batch");
}

void snapshotsAndConcurrentWrites() {
  Fixture a, b; a.configure(); b.configure();
  a.bus->put(0, 1); b.bus->put(0, 2);
  const auto ar = a.bus->reads[0], br = b.bus->reads[0];
  {
    Tx tx(Tx::Read); bool v;
    require(a.pca.readPin(0, v) == ESP_OK && v, "first provider snapshot");
    require(a.pca.readPin(1, v) == ESP_OK && !v, "same provider snapshot reused");
    require(b.pca.readPin(0, v) == ESP_OK && !v, "second provider reads its hardware");
    require(b.pca.readPin(1, v) == ESP_OK && v, "second snapshot reused");
    require(tx.commit() == ESP_OK, "snapshot commit");
  }
  require(a.bus->reads[0] == ar + 1 && b.bus->reads[0] == br + 1, "one input read per provider");
  std::atomic<bool> started{false}, finished{false};
  std::thread other;
  {
    Tx tx(Tx::Write);
    require(a.pca.writePin(8, true) == ESP_OK, "stage first direction pin");
    other = std::thread([&] {
      started = true;
      require(a.pca.writePin(14, true) == ESP_OK, "parallel direct fan write");
      finished = true;
    });
    while (!started) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(!finished && (a.bus->get(2) & 0x4300) == 0, "non-Tx writer excluded from staged batch");
    require(a.pca.writePin(9, false) == ESP_OK && tx.commit() == ESP_OK, "paired direction committed");
  }
  other.join();
  require(finished && (a.bus->get(2) & 0x4300) == 0x4100, "fan merge preserves motor pair");
  {
    Tx tx(Tx::Write);
    require(a.pca.writePin(8, false) == ESP_OK && b.pca.writePin(9, true) == ESP_OK,
            "stage multiple expanders");
    require(tx.commit() == ESP_OK, "commit all providers");
  }
  require((a.bus->get(2) & 0x300) == 0 && (b.bus->get(2) & 0x300) == 0x200,
          "both expanders flushed");
}

void policyAndEightBitErrors() {
  Fixture f; f.configure();
  f.pca.setRecoveryOutput(0x300, 0); // Fan has no explicit safe level.
  f.bus->put(6, 0xffff); f.bus->writes.clear();
  bool value;
  require(f.pca.readPin(0, value) == ESP_ERR_INVALID_STATE && f.bus->writes.empty(),
          "never guess safe levels for uncovered outputs");
  auto* bus = new i2c::MasterDev;
  bus->registers = {255, 255, 0, 255, 0, 0, 0, 0};
  io::Pca95x5 pca(bus, io::pca95x5::Bits::Eight, io::pca95x5::Model::Pca9535C);
  bus->failRead = 0; uint16_t input = 123;
  require(pca.read(Register::Input, input) == ESP_ERR_TIMEOUT && input == 123, "eight-bit read preserves actual error and value");
  require(pca.writePin(8, true) == ESP_ERR_INVALID_ARG && pca.readPin(-1, value) == ESP_ERR_INVALID_ARG,
          "validate pin bounds before shifts");
  require((int(pca.pinFlags()) & int(io::pin::Flags::OpenDrain)) != 0 &&
          (int(pca.pinFlags()) & int(io::pin::Flags::PullUp)) == 0, "9535C capabilities");
}

void inversionAndUnconfiguredPolicy() {
  Fixture f; f.configure();
  require(f.pca.write(Register::Inversion, 1) == ESP_OK, "explicit inversion configuration");
  f.bus->put(4, 0); bool value;
  require(f.pca.readPin(0, value) == ESP_ERR_INVALID_STATE && f.bus->get(4) == 1,
          "inversion-only reset detected and restored");
  f.pca.setRecoveryOutput(0, 0);
  f.bus->failWrite = 2;
  require(f.pca.writePin(9, true) == ESP_ERR_TIMEOUT, "unconfigured policy rejected write");
  require(f.pca.readPin(9, value) == ESP_ERR_INVALID_STATE, "unchanged readback reports interruption once");
  require(f.pca.readPin(9, value) == ESP_OK && !value, "unchanged hardware can resynchronize without recovery writes");
  require(f.pca.writePin(14, true) == ESP_OK && (f.bus->get(2) & 0x300) == 0,
          "fresh operation works without replaying rejected command");
}

int main() {
  resetRecovery();
  failedModeAndInitialization();
  stagingAndFailedCommit();
  snapshotsAndConcurrentWrites();
  policyAndEightBitErrors();
  inversionAndUnconfiguredPolicy();
  std::cout << "PASS: PCA95x5 recovery, initialization, staging, concurrency, snapshots and model/error handling\n";
}
