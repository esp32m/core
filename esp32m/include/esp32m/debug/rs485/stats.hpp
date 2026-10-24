#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>

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
    size_t historyBytes = 0;
  };
  struct PeerSnapshot : Peer {
    Counts recent;
    bool recentAvailable = true;
  };
  struct Snapshot : Summary { std::array<PeerSnapshot, PeerCount> peers{}; };

  // Bounded memory; no allocations or event publication in the transport hook.
  // Use the last 15 COMPLETED one-second buckets: rates cover a true 15 s
  // interval rather than dropping part of the oldest bucket. At most 1 s lag.
  class Stats {
   public:
    // Called before taking the UART lock. Only peers actually requested own a
    // history; allocation failure never prevents the transport operation.
    bool prepare(bool raw, uint8_t address) {
      std::lock_guard preparation(_prepareMutex);
      unsigned index;
      {
        std::lock_guard guard(_mutex);
        index = peer(raw, address);
        if (_history[index]) return true;
      }
      // Do not hold the statistics lock while allocating: a transaction that
      // already owns the UART can still complete its allocation-free update.
      std::unique_ptr<History> history(new (std::nothrow) History);
      if (!history) return false;
      std::lock_guard guard(_mutex);
      _history[index] = std::move(history);
      return true;
    }
    void reset(uint64_t nowMs) {
      // Serialize identity reservation/allocation with reset. record() never
      // takes this mutex, and therefore cannot wait for a heap allocation.
      std::lock_guard preparation(_prepareMutex);
      decltype(_history) retired;
      {
        std::lock_guard guard(_mutex);
        _total = {};
        for (auto& p : _peers) p = {};
        _reserved.fill(false);
        _history.swap(retired);
        for (auto& b : _buckets) b.used = false;
        _started = nowMs; _last = 0; ++_session;
      }
      // Return retired rows to the heap after releasing the completion lock.
    }
    void record(const modbus::Transaction& t, uint64_t nowMs) {
      std::lock_guard guard(_mutex);
      // Reset may win the lock after this completion captured its timestamp.
      if (nowMs < _started) return;
      const auto index = peer(t.raw, t.address);
      auto& p = _peers[index];
      p.used = true; p.raw = t.raw; p.address = t.address; p.lastMs = nowMs;
      p.total.add(t); _total.add(t); _last = nowMs;
      const auto second = nowMs / 1000;
      const auto slot = second % _buckets.size();
      auto& b = _buckets[slot];
      if (!b.used || b.second != second) {
        b.total = {};
        b.missingPeers = 0;
        // All sparse rows share the aggregate bucket's generation. Clearing
        // this one column avoids duplicating a 64-bit timestamp per peer.
        for (const auto& h : _history)
          if (h) (*h)[slot] = {};
        b.used = true; b.second = second;
      }
      b.total.add(t);
      if (auto* h = _history[index].get()) {
        (*h)[slot].add(t);
      } else {
        // A failed preflight allocation, or reset between preflight and
        // completion, only affects per-peer recent values. Preserve exact
        // aggregate and lifetime counts, and never report a partial score.
        b.missingPeers |= uint32_t(1) << index;
      }
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
        out.peers[i].recentAvailable = true;
        for (unsigned slot = 0; slot < _buckets.size(); ++slot) {
          const auto& b = _buckets[slot];
          if (!included(b, end)) continue;
          if (b.missingPeers & (uint32_t(1) << i))
            out.peers[i].recentAvailable = false;
          if (const auto* h = _history[i].get())
            out.peers[i].recent.merge((*h)[slot]);
        }
        if (!out.peers[i].recentAvailable) out.peers[i].recent = {};
      }
    }

   private:
    struct Bucket {
      bool used = false;
      uint64_t second = 0;
      Counts total;
      uint32_t missingPeers = 0;
    };
    using History = std::array<Counts, WindowSeconds + 1>;
    unsigned peer(bool raw, uint8_t address) {
      for (unsigned i = 0; i < MaxPeers; ++i) {
        auto& p = _peers[i];
        if (_reserved[i] && (p.raw != raw || p.address != address)) continue;
        _reserved[i] = true; p.raw = raw; p.address = address;
        return i;
      }
      return MaxPeers;
    }
    static bool included(uint64_t second, uint64_t end) {
      return second < end && end - second <= WindowSeconds;
    }
    bool included(const Bucket& b, uint64_t end) const {
      return b.used && included(b.second, end);
    }
    void summarize(Summary& out, uint64_t nowMs) const {
      out = {}; out.total = _total; out.lastMs = _last; out.session = _session;
      for (const auto& h : _history)
        if (h) out.historyBytes += sizeof(History);
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
    std::mutex _prepareMutex;
    Counts _total;
    std::array<Peer, PeerCount> _peers{};
    std::array<bool, MaxPeers> _reserved{};
    std::array<Bucket, WindowSeconds + 1> _buckets{};
    std::array<std::unique_ptr<History>, PeerCount> _history{};
    uint64_t _started = 0, _last = 0;
    uint32_t _session = 0;
  };
}  // namespace esp32m::debug::rs485
