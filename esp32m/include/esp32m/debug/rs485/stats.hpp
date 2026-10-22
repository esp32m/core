#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "esp32m/bus/diagnostics.hpp"

namespace esp32m::debug::rs485 {
  constexpr unsigned WindowSeconds = 15;
  constexpr unsigned MaxPeers = 16;
  constexpr unsigned PeerCount = MaxPeers + 1;  // Last row is overflow.

  struct Counts {
    uint64_t attempts = 0, success = 0, timeouts = 0, invalid = 0;
    uint64_t other = 0, unverified = 0, durationUs = 0, maxDurationUs = 0;
    uint64_t successDurationUs = 0;

    uint64_t errors() const { return timeouts + invalid + other; }
    double health() const {
      const auto checked = attempts - unverified;
      return checked ? 100.0 * success / checked : NAN;
    }
    double meanMs() const {
      return attempts ? durationUs / (1000.0 * attempts) : NAN;
    }
    double successMs() const {
      return success ? successDurationUs / (1000.0 * success) : NAN;
    }
    double percent(uint64_t count) const {
      const auto checked = attempts - unverified;
      return checked ? 100.0 * count / checked : NAN;
    }
    void add(const modbus::Transaction& t) {
      ++attempts;
      switch (t.outcome) {
        case modbus::Outcome::Success: ++success; successDurationUs += t.durationUs; break;
        case modbus::Outcome::Timeout: ++timeouts; break;
        case modbus::Outcome::InvalidResponse: ++invalid; break;
        case modbus::Outcome::OtherError: ++other; break;
        case modbus::Outcome::Unverified: ++unverified; break;
      }
      durationUs += t.durationUs;
      maxDurationUs = std::max(maxDurationUs, t.durationUs);
    }
    void merge(const Counts& c) {
      attempts += c.attempts; success += c.success; timeouts += c.timeouts;
      invalid += c.invalid; other += c.other; unverified += c.unverified;
      durationUs += c.durationUs; successDurationUs += c.successDurationUs;
      maxDurationUs = std::max(maxDurationUs, c.maxDurationUs);
    }
  };

  struct Peer {
    bool used = false, raw = false;
    uint8_t address = 0;
    Counts total;
    uint64_t lastMs = 0;
  };
  struct Summary {
    Counts recent, total;
    uint64_t elapsedMs = 0, startedAtMs = 0, windowMs = 0, windowEndMs = 0, lastMs = 0;
    uint32_t session = 0;
  };
  struct PeerSnapshot : Peer { Counts recent; };
  struct Snapshot : Summary { std::array<PeerSnapshot, PeerCount> peers{}; };

  // Bounded memory; no allocations or event publication in the transport hook.
  // Use the last 15 COMPLETED one-second buckets: rates cover a true 15 s
  // interval rather than dropping part of the oldest bucket. At most 1 s lag.
  class Stats {
   public:
    void reset(uint64_t nowMs) {
      std::lock_guard guard(_mutex);
      _total = {};
      for (auto& p : _peers) p = {};
      for (auto& b : _buckets) b.used = false;
      _started = nowMs; _last = 0; ++_session;
    }
    void record(const modbus::Transaction& t, uint64_t nowMs) {
      std::lock_guard guard(_mutex);
      // Reset may win the lock after this completion captured its timestamp.
      if (nowMs < _started) return;
      unsigned peer = MaxPeers;
      for (unsigned i = 0; i < MaxPeers; ++i) {
        if (!_peers[i].used || (_peers[i].raw == t.raw &&
                               _peers[i].address == t.address)) {
          peer = i; break;
        }
      }
      auto& p = _peers[peer];
      p.used = true; p.raw = t.raw; p.address = t.address; p.lastMs = nowMs;
      p.total.add(t); _total.add(t); _last = nowMs;
      const auto second = nowMs / 1000;
      auto& b = _buckets[second % _buckets.size()];
      if (!b.used || b.second != second) {
        b.total = {};
        for (auto& c : b.peers) c = {};
        b.used = true; b.second = second;
      }
      b.total.add(t); b.peers[peer].add(t);
    }
    void summary(Summary& out, uint64_t nowMs) const {
      std::lock_guard guard(_mutex);
      summarize(out, nowMs);
    }
    void snapshot(Snapshot& out, uint64_t nowMs) const {
      std::lock_guard guard(_mutex);
      summarize(out, nowMs);
      const auto end = nowMs / 1000;
      for (unsigned i = 0; i < PeerCount; ++i) {
        static_cast<Peer&>(out.peers[i]) = _peers[i];
        out.peers[i].recent = {};
        for (const auto& b : _buckets)
          if (included(b, end)) out.peers[i].recent.merge(b.peers[i]);
      }
    }

   private:
    struct Bucket {
      bool used = false;
      uint64_t second = 0;
      Counts total;
      std::array<Counts, PeerCount> peers{};
    };
    bool included(const Bucket& b, uint64_t end) const {
      return b.used && b.second < end && end - b.second <= WindowSeconds;
    }
    void summarize(Summary& out, uint64_t nowMs) const {
      out = {}; out.total = _total; out.lastMs = _last; out.session = _session;
      out.startedAtMs = _started;
      // The caller's clock sample can precede a concurrent reset by a tick.
      out.elapsedMs = nowMs >= _started ? nowMs - _started : 0;
      out.windowEndMs = nowMs / 1000 * 1000;
      out.windowMs = out.windowEndMs > _started
          ? std::min<uint64_t>(WindowSeconds * 1000, out.windowEndMs - _started) : 0;
      for (const auto& b : _buckets)
        if (included(b, nowMs / 1000)) out.recent.merge(b.total);
    }
    mutable std::mutex _mutex;
    Counts _total;
    std::array<Peer, PeerCount> _peers{};
    std::array<Bucket, WindowSeconds + 1> _buckets{};
    uint64_t _started = 0, _last = 0;
    uint32_t _session = 0;
  };
}  // namespace esp32m::debug::rs485
