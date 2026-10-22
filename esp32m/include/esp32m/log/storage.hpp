#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace esp32m {
  namespace log {
    namespace storage {
      struct Policy {
        size_t quotaBytes = 256 * 1024;
        uint32_t maxAgeDays = 7;  // Zero disables age expiry.
        uint32_t quotaPercent = 0;  // Zero uses quotaBytes; otherwise 1..100.
      };
      struct Capacity {
        size_t total = 0;
        size_t used = 0;  // Current filesystem usage, including managed logs.
        size_t reserve = 0;
      };
      struct Result {
        bool ok = true;
        bool pressure = false;  // Release/cancel snapshots before retrying.
        std::string error;
        explicit operator bool() const { return ok; }
      };
      struct Segment {
        std::string id;
        size_t size = 0;
        bool active = false;
        std::string bootId;
        int64_t firstTime = 0;
        int64_t lastTime = 0;
        bool timeKnown = false;
      };
      struct Status {
        size_t usedBytes = 0;
        size_t effectiveQuotaBytes = 0;
        size_t fileCount = 0;
        std::string lastError;
        size_t quotaBytes = 0;  // Resolved requested budget, before reserve.
        size_t quotaBaseBytes = 0;  // Free space with owned logs reclaimed.
      };
      struct Read {
        std::string text;
        size_t offset = 0;
        size_t nextOffset = 0;
        size_t size = 0;
        bool eof = true;
      };
      struct Snapshot {
        uint64_t token = 0;
        std::vector<Segment> files;
      };

      // Portable checked file storage. The caller serializes EVERY operation,
      // including read/snapshot/status; no internal mutex or worker is created.
      // IdSupplier must reserve a nonzero, durable, strictly increasing ID before
      // returning it (e.g. NVS once per segment, never once per message).
      class Engine {
       public:
        using IdSupplier = std::function<uint64_t()>;
        static constexpr size_t MinimumQuota = 8192;
        static constexpr size_t MaxSegments = 32;
        static constexpr size_t MaxRead = 8192;
        static constexpr size_t MaxRecord = 4096;

        Engine(std::string directory, IdSupplier reserveId);
        ~Engine();
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        Result configure(const Policy& policy, const Capacity& capacity,
                         int64_t nowMs = 0);
        Result recover(const Capacity& capacity, int64_t nowMs = 0);
        Result append(const std::string& text, int64_t utcMs,
                      const std::string& bootId, const Capacity& capacity);
        Result maintenance(int64_t nowMs, const Capacity& capacity);
        Result flush();
        Result seal();
        // Close handles and forget catalog/pins, without deleting any files.
        // Used before a filesystem format/unmount; recover after remount.
        void reset();

        const Policy& policy() const { return _policy; }
        // Last sampled capacity adjusted for this engine's writes/deletions,
        // including stdio data not yet visible in a physical capacity query.
        Capacity accountedCapacity() const { return _capacity; }
        const std::vector<Segment>& files() const { return _files; }
        Status status() const;
        Result read(const std::string& id, size_t offset, size_t limit, Read& out);
        // Empty id snapshots all files, otherwise one strict managed ID.
        // Snapshot endpoints include only completed appends; flush is checked.
        Result snapshot(const std::string& id, Snapshot& out);
        Result readSnapshot(uint64_t token, size_t fileIndex, size_t offset,
                            size_t limit, Read& out);
        void releaseSnapshot(uint64_t token);
        bool hasSnapshots() const { return !_snapshots.empty(); }
        static bool validId(const std::string& id);
        static size_t quotaBase(const Capacity& capacity, size_t ownedLogBytes);
        static size_t quotaBudget(const Policy& policy, const Capacity& capacity,
                                  size_t ownedLogBytes);

       private:
        std::string _directory;
        IdSupplier _reserveId;
        Policy _policy;
        Capacity _capacity;
        std::vector<Segment> _files;
        FILE* _writer = nullptr;
        size_t _activeRecords = 0;
        bool _activeAllTimesKnown = true;
        size_t _effectiveQuota = 0;
        size_t _quotaBudget = 0, _quotaBase = 0;
        std::string _lastError;
        uint64_t _nextSnapshot = 1;
        std::map<uint64_t, Snapshot> _snapshots;

        std::string path(const std::string& id) const;
        size_t used() const;
        size_t target() const;
        void capacity(const Capacity& value);
        bool pinned(const std::string& id) const;
        Segment* active();
        Segment* find(const std::string& id);
        Result fail(const std::string& error, bool pressure = false);
        Result writerFailure(const std::string& error);
        Result prune(size_t extraBytes, size_t extraFiles, int64_t nowMs);
        Result create(const std::string& bootId);
        Result readFile(const Segment& segment, size_t offset, size_t limit,
                        Read& out);
      };
    }  // namespace storage
  }  // namespace log
}  // namespace esp32m
