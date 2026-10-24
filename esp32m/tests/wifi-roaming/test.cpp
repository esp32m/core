#include "esp32m/net/wifi/roaming.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

static unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) \
  throw std::runtime_error("check failed at line " + std::to_string(__LINE__) + ": " #condition); } while (0)

using Policy = esp32m::net::wifi::RoamingPolicy;
using Action = Policy::Action;

static Action poll(Policy& policy, uint32_t now, bool weak = true,
                   bool available = true) {
  return policy.poll(now, true, true, weak, available);
}

static void cooldownAndNeighbors() {
  Policy policy;
  const uint32_t start = 1234;
  policy.connected(start);
  CHECK(poll(policy, start) == Action::None);
  CHECK(poll(policy, start + Policy::AttemptInterval - 1) == Action::None);
  const auto attempt = start + Policy::AttemptInterval;
  CHECK(poll(policy, attempt) == Action::RequestNeighbors);
  CHECK(poll(policy, attempt) == Action::None);
  CHECK(poll(policy, attempt + Policy::NeighborTimeout - 1) == Action::None);
  CHECK(poll(policy, attempt + Policy::NeighborTimeout) == Action::QueryBtm);
  CHECK(poll(policy, attempt + Policy::NeighborTimeout) == Action::None);

  // A missing neighbor report still progresses to BTM and a bounded survey.
  const auto transition = attempt + Policy::NeighborTimeout;
  CHECK(poll(policy, transition + Policy::TransitionTimeout - 1) == Action::None);
  CHECK(poll(policy, transition + Policy::TransitionTimeout) == Action::Scan);
  CHECK(poll(policy, transition + Policy::TransitionTimeout + 600000) == Action::None);
  const auto complete = transition + Policy::TransitionTimeout + 600001;
  policy.scanComplete(complete);
  CHECK(poll(policy, complete) == Action::None);
  CHECK(poll(policy, complete + Policy::AttemptInterval - 1) == Action::None);
  CHECK(poll(policy, complete + Policy::AttemptInterval) == Action::RequestNeighbors);

  // A valid/empty report or synchronous RRM failure follows the same BTM path.
  for (bool report : {false, true}) {
    Policy reply;
    reply.connected(0);
    CHECK(poll(reply, Policy::AttemptInterval) == Action::RequestNeighbors);
    const auto delivered = Policy::AttemptInterval + 2;
    if (report) reply.neighborReport(delivered);
    else reply.neighborRequestFailed(delivered);
    CHECK(poll(reply, delivered) == Action::QueryBtm);
    CHECK(poll(reply, delivered) == Action::None);
    reply.transitionRequestFailed(delivered + 1);
    CHECK(poll(reply, delivered + 1, true, false) == Action::None);
    CHECK(poll(reply, delivered + 100, true, false) == Action::None);
    CHECK(poll(reply, delivered + 101) == Action::Scan);
    CHECK(poll(reply, delivered + 102) == Action::None);
  }
}

static void disabledHealthyAndOutOfOrder() {
  Policy policy;
  policy.connected(0);
  const auto time = Policy::AttemptInterval;
  CHECK(policy.poll(time, false, true, true, true) == Action::None);
  CHECK(policy.poll(time, true, false, true, true) == Action::None);
  CHECK(policy.poll(time, true, true, false, true) == Action::None);
  // An unsolicited neighbor event cannot skip RRM or fabricate a transition.
  policy.neighborReport(time);
  policy.transitionRequestFailed(time);
  CHECK(poll(policy, time) == Action::RequestNeighbors);
  CHECK(poll(policy, time + 1, false) == Action::None);
  CHECK(poll(policy, time + 2) == Action::None);
  CHECK(poll(policy, time + Policy::AttemptInterval) == Action::RequestNeighbors);
  CHECK(policy.poll(time + Policy::AttemptInterval + 1, false, true, true, true) == Action::None);
  policy.neighborReport(time + Policy::AttemptInterval + 2);
  CHECK(poll(policy, time + Policy::AttemptInterval + 3) == Action::None);

  // A successful reassociation cancels in-flight work and restarts its cooldown.
  const auto reconnected = time + Policy::AttemptInterval + 4;
  policy.connected(reconnected);
  CHECK(poll(policy, reconnected + Policy::AttemptInterval - 1) == Action::None);
  CHECK(poll(policy, reconnected + Policy::AttemptInterval) == Action::RequestNeighbors);
  CHECK(policy.poll(reconnected + Policy::AttemptInterval + 1, true, false, true, true) == Action::None);
}

static void failedRoamAndWrap() {
  Policy policy;
  policy.connected(1000);
  CHECK(!policy.associationPending(1000));
  policy.disconnected(true, 2000);
  CHECK(policy.associationPending(2000));
  CHECK(policy.associationPending(2000 + Policy::AssociationTimeout - 1));
  CHECK(!policy.associationPending(2000 + Policy::AssociationTimeout));
  CHECK(!policy.associationPending(2001 + Policy::AssociationTimeout));
  policy.disconnected(true, 30000);
  CHECK(policy.associationPending(30001));
  policy.disconnected(false, 30002);
  CHECK(!policy.associationPending(30002));
  policy.disconnected(true, 40000);
  policy.connected(40001);
  CHECK(!policy.associationPending(40001));

  const auto max = std::numeric_limits<uint32_t>::max();
  for (uint32_t start : {uint32_t(0), max - 5, max - 29999, max - 100000}) {
    Policy wrapped;
    wrapped.connected(start);
    CHECK(poll(wrapped, start + Policy::AttemptInterval - 1) == Action::None);
    const uint32_t attempt = start + Policy::AttemptInterval;
    CHECK(poll(wrapped, attempt) == Action::RequestNeighbors);
    CHECK(poll(wrapped, attempt + Policy::NeighborTimeout - 1) == Action::None);
    CHECK(poll(wrapped, attempt + Policy::NeighborTimeout) == Action::QueryBtm);
    const uint32_t transition = attempt + Policy::NeighborTimeout;
    CHECK(poll(wrapped, transition + Policy::TransitionTimeout - 1) == Action::None);
    CHECK(poll(wrapped, transition + Policy::TransitionTimeout) == Action::Scan);
    wrapped.scanComplete(transition + Policy::TransitionTimeout);
    CHECK(poll(wrapped, transition + Policy::TransitionTimeout + Policy::AttemptInterval) == Action::RequestNeighbors);
    wrapped.disconnected(true, start);
    CHECK(wrapped.associationPending(start + Policy::AssociationTimeout - 1));
    CHECK(!wrapped.associationPending(start + Policy::AssociationTimeout));
  }
}

static void candidates() {
  CHECK(Policy::betterCandidate(-88, -50, -75, false));
  CHECK(!Policy::betterCandidate(-88, -50, -75, true));
  CHECK(!Policy::betterCandidate(-88, -76, -75, false));
  CHECK(!Policy::betterCandidate(-80, -71, -75, false));
  CHECK(Policy::betterCandidate(-80, -70, -75, false));
  CHECK(Policy::betterCandidate(-85, -75, -75, false));
  CHECK(!Policy::betterCandidate(-85, -76, -75, false));
  // Sweep boundaries instead of duplicating the production selector formula.
  for (int current = -100; current < -75; ++current) {
    CHECK(!Policy::betterCandidate(current, current + 9, -100, false));
    CHECK(Policy::betterCandidate(current, current + 10, -100, false));
    CHECK(!Policy::betterCandidate(current, -76, -75, false));
    CHECK(!Policy::betterCandidate(current, -50, -75, true));
  }
}

// Compile the actual initial-connection selection method. These doubles replace
// credentials, STA/AP transport and timing; selection order is production code.
namespace esp32m::net {
  namespace wifi { enum class StaStatus { Initial, Connected }; }
  using wifi::StaStatus;
  struct ApInfo {
    int id;
    bool pinned, pinnedOk, unpinnedOk;
    uint8_t failures = 0;
    const uint8_t* bssid() const {
      static const uint8_t address[6] = {1, 2, 3, 4, 5, 6};
      return pinned ? address : nullptr;
    }
  };
  struct FakeSta {
    StaStatus status = StaStatus::Initial;
    void setStatus(StaStatus value) { status = value; }
  };
  struct FakeAp { int clients = 0; int clientsCount() const { return clients; } };
  struct Wifi {
    std::unique_ptr<ApInfo> _connect;
    std::unique_ptr<FakeAp> _ap;
    std::vector<ApInfo*> _aps;
    FakeSta _sta;
    int _connectFailures = 0;
    std::vector<std::pair<int, bool>> attempts;
    bool tryConnect();
    bool tryConnect(ApInfo* ap, bool ignorePin) {
      attempts.emplace_back(ap->id, ignorePin);
      return ignorePin ? ap->unpinnedOk : ap->pinnedOk;
    }
  };
  void delay(unsigned) {}
#define logW(...) ((void)0)
#include "connection.inc"
#undef logW
}

static void selection() {
  using esp32m::net::ApInfo;
  using esp32m::net::Wifi;
  const std::vector<std::pair<int, bool>> pinnedOnly{{2, false}};
  ApInfo pinned{2, true, true, true, 4};
  ApInfo fallback{1, false, true, true, 0};
  Wifi preferred;
  preferred._aps = {&fallback, &pinned}; // no reliance on vector sort order
  preferred._connectFailures = 9;
  CHECK(preferred.tryConnect());
  CHECK(preferred.attempts == pinnedOnly);
  CHECK(preferred._connectFailures == 0);
  CHECK(preferred._sta.status == esp32m::net::wifi::StaStatus::Connected);
  CHECK(pinned.failures == 4); // the selector never turns this into a blacklist

  pinned.pinnedOk = pinned.unpinnedOk = false;
  Wifi failedPin;
  failedPin._aps = {&pinned, &fallback};
  CHECK(failedPin.tryConnect());
  CHECK((failedPin.attempts == std::vector<std::pair<int, bool>>{{2, false}, {2, true}, {1, true}}));

  ApInfo pin3{3, true, true, true, 200};
  Wifi secondPin;
  secondPin._aps = {&pinned, &fallback, &pin3};
  CHECK(secondPin.tryConnect());
  CHECK((secondPin.attempts == std::vector<std::pair<int, bool>>{{2, false}, {3, false}}));

  Wifi explicitUi;
  explicitUi._aps = {&pin3};
  explicitUi._connect = std::make_unique<ApInfo>(ApInfo{9, true, true, true, 0});
  CHECK(explicitUi.tryConnect());
  CHECK((explicitUi.attempts == std::vector<std::pair<int, bool>>{{9, false}}));
  CHECK(!explicitUi._connect);

  Wifi failedUi;
  failedUi._aps = {&pin3};
  failedUi._connect = std::make_unique<ApInfo>(ApInfo{9, true, false, false, 0});
  CHECK(failedUi.tryConnect());
  CHECK((failedUi.attempts == std::vector<std::pair<int, bool>>{{9, false}, {3, false}}));
  CHECK(!failedUi._connect); // a completed fallback must not suppress roaming

  fallback.unpinnedOk = false;
  Wifi allFailed;
  allFailed._aps = {&pinned, &fallback};
  CHECK(!allFailed.tryConnect());
  CHECK(allFailed._connectFailures == 1);
  CHECK(allFailed._sta.status == esp32m::net::wifi::StaStatus::Initial);

  Wifi captive;
  captive._ap = std::make_unique<esp32m::net::FakeAp>();
  captive._ap->clients = 1;
  captive._aps = {&pin3};
  CHECK(!captive.tryConnect());
  CHECK(captive.attempts.empty());
  CHECK(captive._connectFailures == 0);
}

int main() {
  cooldownAndNeighbors();
  disabledHealthyAndOutOfOrder();
  failedRoamAndWrap();
  candidates();
  selection();
  std::cout << "PASS Wi-Fi roaming policy and production saved-AP selection: "
            << checks << " checks\n";
}
