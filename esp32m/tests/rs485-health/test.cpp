#include "esp32m/debug/rs485/stats.hpp"
#include <cassert>
#include <iostream>
#include <thread>

using namespace esp32m;
using namespace esp32m::debug::rs485;
int main() {
  Stats stats;
  Snapshot s;
  stats.reset(250);
  stats.snapshot(s, 500);
  assert(s.session == 1 && s.windowMs == 0 && std::isnan(s.recent.health()));
  assert(s.startedAtMs == 250 && s.windowEndMs == 0 && s.lastMs == 0);
  stats.record({false, 5, modbus::Outcome::Success, 40000}, 500);
  stats.record({false, 5, modbus::Outcome::Timeout, 1000000}, 800);
  stats.snapshot(s, 999);
  assert(s.total.attempts == 2 && s.recent.attempts == 0);
  stats.snapshot(s, 1000);
  assert(s.windowMs == 750 && s.recent.attempts == 2 && s.recent.health() == 50);
  assert(s.startedAtMs == 250 && s.windowEndMs == 1000 && s.lastMs == 800);
  assert(s.recent.meanMs() == 520 && s.recent.successMs() == 40);
  assert(s.recent.maxDurationUs == 1000000 && s.peers[0].recent.timeouts == 1);
  // Cumulative score is weighted by attempts, not an average of window scores.
  for (int i = 0; i < 8; ++i)
    stats.record({false, 10, modbus::Outcome::Success, 20000}, 1200);
  stats.record({true, 0, modbus::Outcome::Unverified, 10000}, 1300);
  stats.record({true, 0, modbus::Outcome::InvalidResponse, 30000}, 1400);
  stats.record({true, 0, modbus::Outcome::OtherError, 0}, 1401);
  stats.snapshot(s, 2000);
  assert(s.total.attempts == 13 && s.total.health() == 75);
  assert(s.total.errors() == 3 && s.total.unverified == 1);
  assert(s.peers[1].total.health() == 100 && s.peers[0].total.health() == 50);
  assert(s.peers[2].raw && s.peers[2].total.attempts == 3);
  // Exact completed-bucket expiry, no phantom healthy score during silence.
  stats.snapshot(s, 15000);
  assert(s.recent.attempts == 13);
  stats.snapshot(s, 16000);
  assert(s.recent.attempts == 11 && s.windowMs == 15000);
  stats.snapshot(s, 17000);
  assert(s.recent.attempts == 0 && std::isnan(s.recent.health()));
  assert(s.total.attempts == 13 && s.total.health() == 75);
  // Reset drops every old bucket and cumulative value, preserves no stale score.
  stats.reset(17300);
  stats.snapshot(s, 18000);
  assert(s.session == 2 && s.total.attempts == 0 && s.recent.attempts == 0);
  assert(s.startedAtMs == 17300 && s.windowEndMs == 18000 && s.lastMs == 0);
  assert(s.windowMs == 700 && !s.peers[0].used);
  stats.record({false, 5, modbus::Outcome::Timeout, 1000000}, 17299);
  stats.snapshot(s, 17299); // Clock read immediately before a concurrent reset.
  assert(s.total.attempts == 0 && s.elapsedMs == 0 && s.windowMs == 0);
  // A full peer table still accounts for every attempt in an overflow row.
  for (unsigned i = 0; i < MaxPeers + 3; ++i)
    stats.record({false, uint8_t(i + 1), modbus::Outcome::Success, 1000}, 18000);
  stats.snapshot(s, 19000);
  assert(s.peers[MaxPeers].total.attempts == 3);
  assert(s.total.attempts == MaxPeers + 3 && s.recent.attempts == MaxPeers + 3);
  // Monotonic time remains valid beyond the 32-bit millisecond rollover.
  const uint64_t late = (1ULL << 32) + 20000;
  stats.reset(late);
  stats.record({false, 5, modbus::Outcome::Success, 55000}, late + 1);
  stats.snapshot(s, late + 2000);
  assert(s.total.success == 1 && s.recent.success == 1 && s.elapsedMs == 2000);
  assert(s.startedAtMs == late && s.lastMs == late + 1 &&
         s.peers[0].lastMs == late + 1 && s.windowEndMs == (late + 2000) / 1000 * 1000);
  // Reporter/UI snapshots and reset share a small lock with the transport hook.
  stats.reset(late);
  std::thread writer([&] {
    for (int i = 0; i < 10000; ++i)
      stats.record({false, 5, modbus::Outcome::Success, 1000}, late + 100);
  });
  for (int i = 0; i < 1000; ++i) {
    stats.snapshot(s, late + 2000);
    assert(s.total.attempts == s.total.success);
    assert(s.total.attempts == s.peers[0].total.attempts);
  }
  writer.join();
  stats.snapshot(s, late + 2000);
  assert(s.total.attempts == 10000 && s.recent.attempts == 10000);
  std::cout << "RS485 health: windows, warm-up, weighted scores, raw validation, reset, overflow, rollover and concurrent snapshots passed\n";
}
