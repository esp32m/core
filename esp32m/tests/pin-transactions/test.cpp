#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using esp_err_t = int;
constexpr int BIT0 = 1, BIT1 = 2;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
constexpr esp_err_t ESP_ERR_INVALID_STATE = 0x103;
constexpr esp_err_t Failure = 0x107;
static int loggedErrors = 0;
#define ESP_ERROR_CHECK_WITHOUT_ABORT(error) do { (void)(error); ++loggedErrors; } while (false)
#define ESP_CHECK_RETURN(expression) do { const auto result = (expression); if (result != ESP_OK) return result; } while (false)

#include "production-tx.inc"
using esp32m::io::pin::Tx;
using esp32m::io::pin::ITxFinalizer;

static void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

struct Provider : ITxFinalizer {
  int actual = 0, staged = 0, reads = 0, commits = 0, aborts = 0;
  esp_err_t commitError = ESP_OK;
  std::vector<int> events;
  esp_err_t stage(int bit, bool value) {
    std::lock_guard guard(Tx::mutex());
    auto tx = Tx::current();
    require(tx != nullptr, "staged write must own a transaction");
    ESP_CHECK_RETURN(tx->setFinalizer(this));
    staged = value ? (staged | bit) : (staged & ~bit);
    return ESP_OK;
  }
  esp_err_t read(int bit, bool &value) {
    std::lock_guard guard(Tx::mutex());
    auto tx = Tx::current();
    if (!tx || !tx->getReadPerformed(this)) {
      ++reads;
      if (tx)
        tx->setReadPerformed(this);
    }
    value = (actual & bit) != 0;
    return ESP_OK;
  }
  esp_err_t commit() override {
    ++commits;
    if (Tx::current())
      Tx::current()->setReadPerformed(this);  // Verify cache invalidation after callbacks.
    events.push_back(100 + staged);
    if (commitError)
      return commitError;
    actual = staged;
    return ESP_OK;
  }
  void abort() override {
    ++aborts;
    staged = actual;
  }
};

struct Digital {
  Provider &provider;
  int bit;
  esp_err_t writeError = ESP_OK, directionError = ESP_OK;
  esp_err_t write(bool value) {
    if (writeError)
      return writeError;
    return provider.stage(bit, value);
  }
  esp_err_t read(bool &value) { return provider.read(bit, value); }
  esp_err_t setDirection(bool, bool) {
    provider.events.push_back(bit);
    require(provider.actual == 0, "Off latch must be committed before output enable");
    return directionError;
  }
};
struct Pwm {
  bool enabled = false;
  esp_err_t enable(bool value) { enabled = value; return ESP_OK; }
  bool isEnabled() const { return enabled; }
};
struct Pin {
  Digital output;
  Pwm modulation;
  Digital *digital() { return &output; }
  Pwm *pwm() { return &modulation; }
};

namespace esp32m::config {
  struct Changed { static void publish(void *) {} };
}
namespace esp32m::dev {
  // Only Device/platform dependencies are replaced. Every hardware method is
  // compiled from hbridge.cpp unchanged, including initialization and run().
  class HBridge {
   public:
    enum Mode { Off, Forward, Reverse, Brake };
    HBridge(Pin *fwd, Pin *rev) : _fwd(fwd), _rev(rev) {}
    esp_err_t init();
    esp_err_t run(Mode mode, float speed = 1);
    esp_err_t refresh();
    esp_err_t setPins(bool fwd, bool rev);
    esp_err_t getPins(bool &fwd, bool &rev);
    esp_err_t setSpeed(float) { return ESP_OK; }
    bool isPwm() const { return false; }
    Pin *_fwd, *_rev;
    bool _persistent = false, _initialized = false;
    Mode _mode = Off;
  };
}
namespace esp32m::io {
  class Mux74hc {
   public:
    esp_err_t selectChannel(int c);
    int _count = 4;
    Digital *_s0 = nullptr, *_s1 = nullptr, *_s2 = nullptr, *_s3 = nullptr;
  };
}

#include "production-hardware.inc"

static void ownershipAndSerialization() {
  using namespace std::chrono_literals;
  std::promise<void> attempted, entered;
  auto attempt = attempted.get_future();
  auto entry = entered.get_future();
  std::thread worker;
  {
    Tx tx(Tx::Write);
    worker = std::thread([&] {
      require(Tx::current() == nullptr, "another task must not see the owner's Tx");
      require(!Tx::mutex().try_lock(), "all transactions must use one shared mutex");
      attempted.set_value();
      Tx other(Tx::Write);
      require(Tx::current() == &other, "second task must own its own Tx");
      entered.set_value();
    });
    attempt.wait();
    require(entry.wait_for(25ms) == std::future_status::timeout,
            "second task must remain excluded while a batch is open");
    require(Tx::current() == &tx, "other task must not change owner association");
  }
  worker.join();
  require(Tx::current() == nullptr, "outer destructor must clear task association");

  std::promise<void> directAttempt, directEntry;
  auto direct = directEntry.get_future();
  {
    Tx tx(Tx::Read);
    worker = std::thread([&] {
      directAttempt.set_value();
      std::lock_guard guard(Tx::mutex());
      require(Tx::current() == nullptr, "non-Tx provider access remains non-Tx");
      directEntry.set_value();
    });
    directAttempt.get_future().wait();
    require(direct.wait_for(25ms) == std::future_status::timeout,
            "non-Tx provider access must also wait for the full batch");
  }
  worker.join();
  std::cout << "PASS: task ownership and transactional/nontransactional exclusion\n";
}

static void nestedScopesAndProviders() {
  Provider a, b;
  {
    Tx outer(Tx::Write);
    require(a.stage(1, true) == ESP_OK, "first provider staging");
    {
      Tx inner(Tx::Read);
      require(Tx::current() == &inner, "nested scope must become current");
      require((inner.type() & Tx::Write) != 0, "nested type must retain outer write capability");
      bool value;
      a.read(1, value);
      a.read(2, value);
      b.read(1, value);
      b.read(2, value);
      require(a.reads == 1 && b.reads == 1, "read snapshot must be keyed by provider");
      require(b.stage(2, true) == ESP_OK, "second provider staging");
      require(inner.hasFinalizer(&a) && inner.hasFinalizer(&b), "batch must retain both providers");
    }
    require(Tx::current() == &outer, "nested destructor must restore live outer scope");
    require(a.commits == 0 && b.commits == 0, "nested destruction must retain batch");
    {
      Tx inner(Tx::Write);
      require(inner.commit() == ESP_OK, "explicit inner commit must flush shared batch");
      require(a.actual == 1 && b.actual == 2, "all provider writes must commit");
      require(!inner.getReadPerformed(&a) && !inner.getReadPerformed(&b),
              "commit must clear even cache keys set during finalizer callbacks");
    }
    require(outer.commit() == ESP_OK, "empty repeated commit must succeed");
  }
  require(a.commits == 1 && b.commits == 1, "providers commit once, without destructor replay");
  {
    Tx outer(Tx::Write);
    a.stage(4, true);
    {
      Tx inner(Tx::Write);
      b.stage(8, true);
      inner.abort();
    }
    require(outer.commit() == ESP_ERR_INVALID_STATE, "inner abort must cancel outer pending batch");
    require(a.stage(16, true) == ESP_ERR_INVALID_STATE, "aborted scope must reject new staging");
  }
  require(a.actual == 1 && b.actual == 2 && a.staged == 1 && b.staged == 2,
          "nested abort must discard pending changes on all providers");
  std::cout << "PASS: nested lifetime, multiple providers, read caches and abort\n";
}

static void commitFailureAndLegacyScope() {
  Provider a, b;
  a.commitError = Failure;
  {
    Tx tx(Tx::Write);
    a.stage(1, true);
    b.stage(2, true);
    require(tx.commit() == Failure, "explicit commit must propagate I/O error");
    require(tx.commit() == Failure, "failed commit must not retry silently");
  }
  require(a.commits == 1 && b.commits == 0 && a.aborts == 1 && b.aborts == 1,
          "failure must discard failed and uncommitted providers");
  require(a.staged == 0 && b.staged == 0, "failed staged values must never replay");
  esp_err_t err = ESP_OK;
  {
    Tx tx(Tx::Write, &err);
    a.stage(1, true);
  }
  require(err == Failure, "legacy error pointer still receives destructor error");
  a.commitError = ESP_OK;
  {
    Tx tx(Tx::Write);
    a.stage(2, true);
  }
  require(a.actual == 2, "later batch must contain only new staged bits");
  std::cout << "PASS: commit errors, legacy RAII compatibility and no replay\n";
}

static void bridgeAndMux() {
  using esp32m::dev::HBridge;
  Provider provider;
  provider.actual = provider.staged = 3;  // Retained moving/braking outputs.
  Pin forward{{provider, 1}, {}}, reverse{{provider, 2}, {}};
  HBridge bridge(&forward, &reverse);
  provider.commitError = Failure;
  require(bridge.init() == Failure && !bridge._initialized,
          "failed Off initialization must propagate and remain uninitialized");
  require(provider.events.size() == 1, "failed Off commit must not enable directions");
  provider.commitError = ESP_OK;
  provider.events.clear();
  require(bridge.init() == ESP_OK, "initialization must recover after transient failure");
  require(provider.events == std::vector<int>({100, 1, 2}),
          "initialization must commit Off before either direction");
  require(bridge._mode == HBridge::Off, "startup must not adopt retained moving mode");

  provider.commitError = Failure;
  require(bridge.setPins(true, false) == Failure,
          "HBridge must return deferred bus commit error");
  provider.commitError = ESP_OK;
  reverse.output.writeError = Failure;
  const int before = provider.commits;
  require(bridge.setPins(true, true) == Failure, "second pin staging failure propagates");
  require(provider.commits == before && provider.staged == provider.actual,
          "second pin failure must abort first pin staging");
  reverse.output.writeError = ESP_OK;
  require(bridge.setPins(false, true) == ESP_OK && provider.actual == 2,
          "next command must not replay aborted forward pin");

  esp32m::io::Mux74hc mux;
  mux._s0 = &forward.output;
  mux._s1 = &reverse.output;
  provider.commitError = Failure;
  require(mux.selectChannel(1) == Failure, "mux must return commit error before scope exits");
  provider.commitError = ESP_OK;
  reverse.output.writeError = Failure;
  const int beforeMux = provider.commits;
  require(mux.selectChannel(3) == Failure && provider.commits == beforeMux,
          "mux staging failure must abort without a partial commit");
  require(mux.selectChannel(4) == ESP_ERR_INVALID_ARG, "mux range check remains intact");
  require(loggedErrors == 0, "explicitly returned failures need no destructor-only log");
  std::cout << "PASS: HBridge startup ordering, commit/staging failures and mux errors\n";
}

int main() {
  ownershipAndSerialization();
  nestedScopesAndProviders();
  commitFailureAndLegacyScope();
  bridgeAndMux();
  std::cout << "All production GPIO transaction/HBridge regression tests passed.\n";
}
