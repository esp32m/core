#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include "esp_core_dump.h"
#include "esp_partition.h"

#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/json.hpp"
#include "esp32m/logging.hpp"
#include "esp32m/ui.hpp"
#include "esp32m/ui/asset.hpp"

namespace esp32m {
  namespace debug {

    class CoreDump : public log::Loggable, public ui::ReadableAsset {
     public:
      CoreDump(const CoreDump&) = delete;
      const char* name() const override {
        return "core-dump";
      }
      static CoreDump& instance() {
        static CoreDump instance;
        return instance;
      }
      bool isAvailable() const {
        return _info.size > 0;
      }
      const std::string& uid() const {
        return _uid;
      }
      std::unique_ptr<stream::Reader> createReader() const override {
        if (_info.size == 0)
          return nullptr;
        return std::make_unique<Reader>(_info.part, _info.addr, _info.size);
      }

     private:
      class Reader : public stream::Reader {
       public:
        Reader(const esp_partition_t* part, size_t addr, size_t size)
            : _part(part), _addr(addr), _size(size), _offset(0) {}
        size_t read(uint8_t* buf, size_t size) override {
          if (_offset >= _size)
            return 0;
          if (_offset + size > _size)
            size = _size - _offset;
          esp_err_t err = esp_partition_read(
              _part, _addr - _part->address + _offset, buf, size);
          if (err != ESP_OK)
            return 0;
          _offset += size;
          return size;
        }

       private:
        const esp_partition_t* _part;
        size_t _addr;
        size_t _size;
        size_t _offset;
      };
      struct {
        const esp_partition_t* part;
        size_t addr;
        size_t size;
      } _info;
      std::string _uid;

      CoreDump() {
        ui::AssetInfo info{"/debug/coredump.bin", "application/octet-stream",
                           nullptr, nullptr};
        init(info);
        if (detect() == ESP_OK)
          Ui::instance().addAsset(this);
      }

      esp_err_t detect() {
        memset(&_info, 0, sizeof(_info));
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
        const esp_partition_t* part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
            NULL /* label */);

        if (!part)
          // No coredump partition in the partition table.
          return ESP_ERR_NOT_FOUND;

        // Quick validity check. If you only want to know whether "something" is
        // there, you could skip this call and just use
        // esp_core_dump_image_get().
        auto err = esp_core_dump_image_check();
        if (err != ESP_OK)
          return err;

        size_t addr = 0, size = 0;
        ESP_CHECK_RETURN(esp_core_dump_image_get(&addr, &size));

        // Sanity: make sure image lives inside the partition.
        if (addr < part->address)
          return ESP_ERR_INVALID_SIZE;

        const size_t rel = addr - part->address;
        if (rel + size > part->size)
          return ESP_ERR_INVALID_SIZE;

#  if CONFIG_ESP_COREDUMP_CHECKSUM_SHA256
        const size_t sum_len = 32;
#  elif CONFIG_ESP_COREDUMP_CHECKSUM_CRC32
        const size_t sum_len = 4;
#  else
        const size_t sum_len = 0;
#  endif
        if (sum_len > 0) {
          uint8_t sum[sum_len];
          const size_t rel_off = (addr - part->address);
          const size_t sum_off = rel_off + size - sum_len;
          ESP_CHECK_RETURN(esp_partition_read(part, sum_off, sum, sum_len));
          _uid = hex_encode(sum, sum_len);
        }

        _info.part = part;
        _info.addr = addr;
        _info.size = size;
        logI("found at 0x%zx, size %zu, UID %s", addr, size, _uid.c_str());
        return ESP_OK;
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif  // CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
      }
    };

    class CrashGuard : public AppObject {
     public:
      enum class Checkpoint : uint8_t {
        Boot = 0,
        Init0 = 10,
        Inited = 20,
        OtaReady = 30,
        Stable = 40,
        Done = 250,
      };

      // Per-slot UI classification.
      // Numeric values are part of the JSON schema.
      enum class OtaRole : uint8_t {
        Preferred = 0,  // OK and considered the most recent / intended image
        Fallback = 1,   // OK but considered older / not intended
        Failed = 2,     // Rollback marked INVALID/ABORTED (if available)
      };

      struct Run {
        uint32_t bootCount = 0;
        uint32_t resetReason = 0;
        uint32_t uptimeMs = 0;
        // Earliest known wall-clock timestamp for this run (seconds since
        // epoch). 0 means unknown/unset (e.g., time not synchronized during the
        // run).
        int64_t ts = 0;
        Checkpoint checkpoint = Checkpoint::Boot;
        bool unexpectedReset = false;
        bool plannedReset = false;
        // OTA slot index for the run: 0/1 for ota_0/ota_1, -1 if unknown.
        int8_t runningSlot = -1;
        std::string crashSig;
        std::string coredumpUid;

        void toJson(JsonObject node) const {
          json::to(node, "boot", bootCount);
          json::to(node, "rr", (int)resetReason);
          json::to(node, "uptime", (int)uptimeMs);
          json::to(node, "ts", (int64_t)ts);
          json::to(node, "cp", (int)checkpoint);
          json::to(node, "unexpected", unexpectedReset);
          json::to(node, "planned", plannedReset);
          json::to(node, "slot", (int)runningSlot);
          json::to(node, "sig", crashSig);
          if (!coredumpUid.empty()) {
            auto cd = node["coredump"].to<JsonObject>();
            json::to(cd, "uid", coredumpUid);
          }
        }
        bool fromJson(JsonVariantConst node, bool& changed) {
          json::from(node["boot"], bootCount, &changed);
          int rr = (int)resetReason;
          if (json::from(node["rr"], rr, &changed))
            resetReason = (uint32_t)rr;
          int up = (int)uptimeMs;
          if (json::from(node["uptime"], up, &changed))
            uptimeMs = (uint32_t)up;
          int64_t t = (int64_t)ts;
          if (json::from(node["ts"], t, &changed))
            ts = t;
          int cp = (int)checkpoint;
          if (json::from(node["cp"], cp, &changed))
            checkpoint = (Checkpoint)cp;
          json::from(node["unexpected"], unexpectedReset, &changed);
          json::from(node["planned"], plannedReset, &changed);
          int slot = (int)runningSlot;
          if (json::from(node["slot"], slot, &changed))
            runningSlot = (int8_t)slot;
          else {
            // Backward compat: older persisted schema used
            // "part":"ota0"/"ota1".
            std::string part;
            if (json::from(node["part"], part, &changed)) {
              if (part == "ota0")
                runningSlot = 0;
              else if (part == "ota1")
                runningSlot = 1;
              else
                runningSlot = -1;
            }
          }
          json::from(node["sig"], crashSig, &changed);
          auto cd = node["coredump"].as<JsonObjectConst>();
          if (cd)
            json::from(cd["uid"], coredumpUid, &changed);
          return changed;
        }
      };

      struct Slot {
        // Heuristic health score for this OTA slot.
        // Increased on firmware-like unexpected resets (with heavier weight for
        // very early crashes), decreased after a stable run.
        int32_t strike = 0;

        // Count of firmware-like crashes attributed to this slot.
        // (Panics/WDT etc. as decided by CrashGuard; excludes planned resets.)
        uint32_t crashes = 0;

        // Count of power-like resets (brownouts) attributed to this slot.
        uint32_t brownouts = 0;

        // Consecutive "early" firmware-like crashes (uptime < EarlyCrashMs).
        // Used as a stronger signal for immediate slot switching.
        uint32_t consecutiveEarly = 0;

        // Best observed stable uptime for this slot in milliseconds.
        // Updated once per boot when the run reaches StableMs.
        uint32_t lastGoodUptimeMs = 0;

        // Last crash signature attributed to this slot (e.g. coredump UID, or
        // reset reason + checkpoint). Used to amplify repeated identical
        // crashes.
        std::string lastCrashSig;

        void toJson(JsonObject node) const {
          json::to(node, "strike", (int)strike);
          json::to(node, "crashes", (int)crashes);
          json::to(node, "brownouts", (int)brownouts);
          json::to(node, "early", (int)consecutiveEarly);
          json::to(node, "good", (int)lastGoodUptimeMs);
          json::to(node, "sig", lastCrashSig);
        }
        bool fromJson(JsonVariantConst node, bool& changed) {
          int s = (int)strike;
          if (json::from(node["strike"], s, &changed))
            strike = s;
          json::from(node["crashes"], crashes, &changed);
          json::from(node["brownouts"], brownouts, &changed);
          json::from(node["early"], consecutiveEarly, &changed);
          json::from(node["good"], lastGoodUptimeMs, &changed);
          json::from(node["sig"], lastCrashSig, &changed);
          return changed;
        }
      };

      struct State {
        // Schema version for the JSON file representation.
        // Allows forward-compatible upgrades if the persisted format changes.
        uint32_t version = 1;

        // Monotonic boot counter (authoritative source: NVS).
        // Used for correlating runs and for anti ping-pong gating.
        uint32_t bootCount = 0;

        // Boot number when CrashGuard last switched the boot partition
        // (authoritative source: NVS).
        uint32_t lastSwitchBoot = 0;

        // Last finalized run summary (typically the previous boot), merged from
        // RTC/NVS and optionally persisted to FS for visibility.
        Run last;

        // Small ring buffer (newest first) of recent finalized runs.
        // This is primarily a UI/diagnostics aid and is persisted to FS.
        std::vector<Run> history;

        // Aggregated health counters per OTA slot (authoritative source: NVS).
        Slot slot0;
        Slot slot1;

        void toJson(JsonObject node) const {
          json::to(node, "ver", (int)version);
          json::to(node, "boot", bootCount);
          json::to(node, "lastSwitch", (int)lastSwitchBoot);
          last.toJson(node["last"].to<JsonObject>());

          auto h = node["history"].to<JsonArray>();
          for (auto& r : history) r.toJson(h.add<JsonObject>());

          slot0.toJson(node["ota0"].to<JsonObject>());
          slot1.toJson(node["ota1"].to<JsonObject>());
        }
        bool fromJson(JsonVariantConst node, bool& changed) {
          int v = (int)version;
          if (json::from(node["ver"], v, &changed))
            version = (uint32_t)v;
          json::from(node["boot"], bootCount, &changed);
          json::from(node["lastSwitch"], lastSwitchBoot, &changed);
          auto ln = node["last"].as<JsonObjectConst>();
          if (ln)
            last.fromJson(ln, changed);

          auto ha = node["history"].as<JsonArrayConst>();
          if (ha) {
            history.clear();
            for (auto r : ha) {
              Run run;
              run.fromJson(r, changed);
              history.push_back(run);
              if (history.size() >= 16)
                break;
            }
          }

          auto o0 = node["ota0"].as<JsonObjectConst>();
          if (o0)
            slot0.fromJson(o0, changed);
          auto o1 = node["ota1"].as<JsonObjectConst>();
          if (o1)
            slot1.fromJson(o1, changed);
          return changed;
        }
      };

      CrashGuard(const CrashGuard&) = delete;
      const char* name() const override {
        return "crash-guard";
      }
      static CrashGuard& instance() {
        static CrashGuard instance;
        return instance;
      }

      void handleEvent(Event& ev) override;
      JsonDocument* getState(RequestContext& ctx) override;
      JsonDocument* getInfo(RequestContext& ctx) override;
      bool handleRequest(Request& req) override;

     private:
      State _state = {};
      constexpr static const char* FilePath = "/crashguard.json";

      CrashGuard();
      void loadStateFromFs();
      void saveStateToFs();

      void onBoot();
      void onPeriodic();
      void onDone(DoneReason reason);
      void setCheckpoint(Checkpoint cp);

      bool _fsReady = false;
      bool _savedStableThisBoot = false;
      uint32_t _bootMs = 0;
      uint32_t _lastFsSaveMs = 0;

      Run _pendingPrevRun = {};
      bool _havePendingPrevRun = false;
    };
  }  // namespace debug

}  // namespace esp32m
