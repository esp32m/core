#include "esp32m/debug/rs485/stats.hpp"
#include <cassert>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <new>
#include <thread>

using namespace esp32m;
using namespace esp32m::debug::rs485;

namespace {
  std::atomic<bool> failHistoryAllocation{false};
  std::atomic<bool> pauseHistoryAllocation{false}, historyAllocationPaused{false};
  std::atomic<unsigned> historyAllocations{0};
}

// Histories use the fallible allocation overload. Inject failure and a slow
// allocator without altering the production storage/transport code.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (failHistoryAllocation) return nullptr;
  if (pauseHistoryAllocation) {
    historyAllocationPaused = true;
    while (pauseHistoryAllocation) std::this_thread::yield();
  }
  try {
    auto* memory = ::operator new(size);
    ++historyAllocations;
    return memory;
  } catch (...) {
    return nullptr;
  }
}

void sparseHistory() {
  Stats stats;
  Snapshot snapshot;
  stats.reset(0);
  stats.snapshot(snapshot, 1000);
  assert(snapshot.historyBytes == 0);
  for (unsigned peer = 0; peer < 6; ++peer) {
    assert(stats.prepare(false, uint8_t(peer + 1)));
    stats.record({false, uint8_t(peer + 1), modbus::Outcome::Success, 1234}, 100);
  }
  stats.snapshot(snapshot, 1000);
  const auto historySize = sizeof(Counts) * (WindowSeconds + 1);
  assert(snapshot.historyBytes == 6 * historySize);
  assert(snapshot.recent.attempts == 6);
  for (unsigned peer = 0; peer < 6; ++peer)
    assert(snapshot.peers[peer].recentAvailable &&
           snapshot.peers[peer].recent.success == 1);
  const auto allocated = historyAllocations.load();
  // Completion under the UART lock must not allocate, including first use of
  // a new peer when reset or memory pressure removed its prepared history.
  failHistoryAllocation = true;
  stats.record({false, 1, modbus::Outcome::Timeout, 500000}, 1200);
  stats.record({true, 0, modbus::Outcome::Unverified, 42}, 1201);
  assert(historyAllocations == allocated);
  failHistoryAllocation = false;
  stats.snapshot(snapshot, 2000);
  assert(snapshot.total.attempts == 8 && snapshot.recent.attempts == 8);
  assert(snapshot.peers[0].recentAvailable && snapshot.peers[0].recent.timeouts == 1);
  assert(!snapshot.peers[6].recentAvailable && snapshot.peers[6].recent.attempts == 0);
  assert(snapshot.peers[6].total.unverified == 1);
  stats.reset(2100);
  stats.snapshot(snapshot, 3000);
  assert(snapshot.historyBytes == 0 && snapshot.total.attempts == 0);
  std::cout << "Sparse RS485: zero idle history, six rows " << 6 * historySize
            << " B, Stats " << sizeof(Stats) << " B, allocation-free completion and reset release passed\n";
}

void fullCapacity() {
  Stats stats;
  Snapshot snapshot;
  stats.reset(0);
  // All sixteen named peers plus three extra identities must retain exact
  // full-window values; a sparse representation may not reduce capacity.
  for (unsigned second = 0; second <= 20; ++second)
    for (unsigned peer = 0; peer < MaxPeers + 3; ++peer) {
      const auto raw = peer == 0;
      const auto address = uint8_t(peer);
      assert(stats.prepare(raw, address));
      stats.record({raw, address, modbus::Outcome::Success, second * 1000 + peer},
                   second * 1000 + 100);
    }
  stats.snapshot(snapshot, 21000);
  assert(snapshot.historyBytes == PeerCount * sizeof(Counts) * (WindowSeconds + 1));
  assert(snapshot.total.success == (MaxPeers + 3) * 21);
  assert(snapshot.recent.success == (MaxPeers + 3) * WindowSeconds);
  Counts combined;
  for (unsigned peer = 0; peer < PeerCount; ++peer) {
    const auto& row = snapshot.peers[peer];
    assert(row.used && row.recentAvailable);
    assert(row.recent.attempts == (peer == MaxPeers ? 3 : 1) * WindowSeconds);
    assert(row.total.attempts == (peer == MaxPeers ? 3 : 1) * 21);
    combined.merge(row.recent);
  }
  assert(combined.attempts == snapshot.recent.attempts &&
         combined.durationUs == snapshot.recent.durationUs &&
         combined.maxDurationUs == snapshot.recent.maxDurationUs);
  stats.snapshot(snapshot, 36000);
  assert(snapshot.recent.attempts == 0 && snapshot.total.success == (MaxPeers + 3) * 21);
  for (const auto& row : snapshot.peers) assert(row.recentAvailable && !row.recent.attempts);
}

void historyFailure() {
  Stats stats;
  Snapshot snapshot;
  stats.reset(0);
  failHistoryAllocation = true;
  assert(!stats.prepare(false, 7));
  stats.record({false, 7, modbus::Outcome::Success, 123456789012ULL}, 100);
  stats.record({false, 7, modbus::Outcome::Timeout, 987654321012ULL}, 1100);
  stats.snapshot(snapshot, 1000);
  assert(!snapshot.peers[0].recentAvailable && snapshot.peers[0].recent.attempts == 0);
  assert(snapshot.recent.success == 1 && snapshot.total.attempts == 2);
  assert(snapshot.total.durationUs == 1111111110024ULL);
  failHistoryAllocation = false;
  assert(stats.prepare(false, 7));
  stats.record({false, 7, modbus::Outcome::Success, 1200}, 1200);
  // An allocated row does not retroactively repair either missing second.
  stats.snapshot(snapshot, 16000);
  assert(!snapshot.peers[0].recentAvailable && snapshot.recent.attempts == 2);
  stats.record({false, 7, modbus::Outcome::Success, 2200}, 16001);
  stats.snapshot(snapshot, 17000);
  assert(snapshot.peers[0].recentAvailable && snapshot.peers[0].recent.attempts == 1);
  assert(snapshot.recent.success == 1 && snapshot.total.attempts == 4);
  // A reset can occur after preflight but before transaction completion.
  stats.reset(18000);
  stats.record({false, 7, modbus::Outcome::OtherError, 10}, 18001);
  stats.snapshot(snapshot, 19000);
  assert(snapshot.total.other == 1 && snapshot.recent.other == 1 &&
         !snapshot.peers[0].recentAvailable);
}

void preparationConcurrency() {
  Stats stats;
  Snapshot snapshot;
  stats.reset(0);
  pauseHistoryAllocation = true;
  historyAllocationPaused = false;
  auto preparing = std::async(std::launch::async, [&] { return stats.prepare(false, 4); });
  while (!historyAllocationPaused) std::this_thread::yield();
  auto completion = std::async(std::launch::async, [&] {
    stats.record({false, 4, modbus::Outcome::Success, 1000}, 100);
  });
  assert(completion.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  completion.get();
  stats.snapshot(snapshot, 1000);
  assert(snapshot.total.success == 1 && !snapshot.peers[0].recentAvailable);
  std::atomic<bool> resetEntered{false};
  auto resetting = std::async(std::launch::async, [&] {
    resetEntered = true;
    stats.reset(2000);
  });
  while (!resetEntered) std::this_thread::yield();
  assert(resetting.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
  pauseHistoryAllocation = false;
  assert(preparing.get());
  resetting.get();
  stats.snapshot(snapshot, 3000);
  assert(snapshot.historyBytes == 0 && snapshot.total.attempts == 0 &&
         !snapshot.peers[0].used);
  // Waiting requests reserve identities before completion; concurrent
  // preparations must not duplicate rows or overwrite another peer's history.
  std::array<std::thread, MaxPeers> preparers;
  stats.reset(0);
  for (unsigned i = 0; i < MaxPeers; ++i)
    preparers[i] = std::thread([&, i] { assert(stats.prepare(false, uint8_t(i))); });
  for (auto& thread : preparers) thread.join();
  stats.snapshot(snapshot, 1000);
  assert(snapshot.historyBytes == MaxPeers * sizeof(Counts) * (WindowSeconds + 1));
  for (const auto& peer : snapshot.peers) assert(!peer.used);
  for (unsigned i = 0; i < MaxPeers; ++i)
    stats.record({false, uint8_t(i), modbus::Outcome::Success, 1}, 100);
  stats.snapshot(snapshot, 1000);
  assert(snapshot.total.success == MaxPeers && !snapshot.peers[MaxPeers].used);
  for (unsigned i = 0; i < MaxPeers; ++i)
    assert(snapshot.peers[i].recentAvailable && snapshot.peers[i].recent.success == 1);
}

int main() {
  sparseHistory();
  fullCapacity();
  historyFailure();
  preparationConcurrency();
  Stats stats;
  Snapshot s;
  const auto record = [&](const modbus::Transaction& transaction, uint64_t nowMs) {
    assert(stats.prepare(transaction.raw, transaction.address));
    stats.record(transaction, nowMs);
  };
  stats.reset(250);
  stats.snapshot(s, 500);
  assert(s.session == 1 && s.windowMs == 0 && std::isnan(s.recent.health()));
  assert(s.startedAtMs == 250 && s.windowEndMs == 0 && s.lastMs == 0);
  record({false, 5, modbus::Outcome::Success, 40000}, 500);
  record({false, 5, modbus::Outcome::Timeout, 1000000}, 800);
  stats.snapshot(s, 999);
  assert(s.total.attempts == 2 && s.recent.attempts == 0);
  stats.snapshot(s, 1000);
  assert(s.windowMs == 750 && s.recent.attempts == 2 && s.recent.health() == 50);
  assert(s.startedAtMs == 250 && s.windowEndMs == 1000 && s.lastMs == 800);
  assert(s.recent.meanMs() == 520 && s.recent.successMs() == 40);
  assert(s.recent.maxDurationUs == 1000000 && s.peers[0].recent.timeouts == 1);
  // Cumulative score is weighted by attempts, not an average of window scores.
  for (int i = 0; i < 8; ++i)
    record({false, 10, modbus::Outcome::Success, 20000}, 1200);
  record({true, 0, modbus::Outcome::Unverified, 10000}, 1300);
  record({true, 0, modbus::Outcome::InvalidResponse, 30000}, 1400);
  record({true, 0, modbus::Outcome::OtherError, 0}, 1401);
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
  record({false, 5, modbus::Outcome::Timeout, 1000000}, 17299);
  stats.snapshot(s, 17299); // Clock read immediately before a concurrent reset.
  assert(s.total.attempts == 0 && s.elapsedMs == 0 && s.windowMs == 0);
  // A full peer table still accounts for every attempt in an overflow row.
  for (unsigned i = 0; i < MaxPeers + 3; ++i)
    record({false, uint8_t(i + 1), modbus::Outcome::Success, 1000}, 18000);
  stats.snapshot(s, 19000);
  assert(s.peers[MaxPeers].total.attempts == 3);
  assert(s.total.attempts == MaxPeers + 3 && s.recent.attempts == MaxPeers + 3);
  // Monotonic time remains valid beyond the 32-bit millisecond rollover.
  const uint64_t late = (1ULL << 32) + 20000;
  stats.reset(late);
  record({false, 5, modbus::Outcome::Success, 55000}, late + 1);
  stats.snapshot(s, late + 2000);
  assert(s.total.success == 1 && s.recent.success == 1 && s.elapsedMs == 2000);
  assert(s.startedAtMs == late && s.lastMs == late + 1 &&
         s.peers[0].lastMs == late + 1 && s.windowEndMs == (late + 2000) / 1000 * 1000);
  // Reporter/UI snapshots and reset share a small lock with the transport hook.
  stats.reset(late);
  assert(stats.prepare(false, 5));
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
