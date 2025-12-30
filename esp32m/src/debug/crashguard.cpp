#include "esp32m/debug/crashguard.hpp"

#include <algorithm>
#include <cstring>

#include <time.h>

#include <esp_app_format.h>
#include <esp_attr.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs.h>

namespace esp32m {
  namespace debug {

    namespace {

      // Bump magic when RTC record layout changes.
      constexpr uint32_t RtcMagic = 0xC0A5'7A9Eu;

          struct RtcRunRecord {
        // Sentinel values used to validate that RTC memory content is
        // initialized and not random (e.g., after power loss).
        uint32_t magic;
        uint32_t magicInv;

        // Monotonic boot counter as last recorded by CrashGuard.
        // This is mirrored from NVS on boot and then kept here for the
        // "previous run" finalization after an unexpected reset.
        uint32_t bootCount;

        // OTA slot index for the *current* run: 0/1 for ota_0/ota_1, 0xFF if
        // unknown. This lets us attribute crashes to the correct slot even if
        // we reboot into the same slot repeatedly.
        uint8_t slot;

        // Last reached lifecycle checkpoint for the current run.
        // Updated as the system progresses (Boot -> Init0 -> Inited -> Stable
        // -> Done).
        uint8_t checkpoint;

        // Whether the run is considered "in progress".
        // 1 while the firmware is running; cleared to 0 only on a planned
        // shutdown path (EventDone), so that a reset with inProgress=1 is
        // treated as unexpected.
        uint8_t inProgress;

        // Whether the next reset is expected/planned (e.g., OTA switch or
        // normal restart). When set, the next boot will avoid counting the
        // reset as a crash.
        uint8_t plannedReset;

        // Best-effort uptime estimate for the current run in milliseconds.
        // Updated periodically; used on the next boot to classify early vs
        // stable crashes.
        uint32_t lastUptimeMs;

        // Earliest known wall-clock timestamp during this run (seconds since epoch).
        // Set once when time() looks valid.
        int64_t firstEpochSec;

        // Crash signature for clustering and amplification.
        // Prefers coredump UID when available; otherwise uses reset reason +
        // checkpoint. Persisted across resets to allow comparing against the
        // last signature.
        char lastCrashSig[64];
      };

      RTC_NOINIT_ATTR static RtcRunRecord s_rtc;

      struct NvsSlotState {
        int32_t strike;
        uint32_t crashes;
        uint32_t brownouts;
        uint32_t consecutiveEarly;
        uint32_t lastGoodUptimeMs;
        char lastCrashSig[64];
      };

      struct NvsState {
        uint32_t version;
        uint32_t bootCount;
        uint32_t lastSwitchBoot;
        NvsSlotState slot[2];
      };

      constexpr uint32_t NvsVersion = 1;
      constexpr const char* NvsNamespace = "crashguard";
      constexpr const char* NvsKey = "state";

      constexpr uint32_t EarlyCrashMs = 60 * 1000;
      constexpr uint32_t StableMs = 10 * 60 * 1000;
      constexpr uint32_t FsSavePeriodMs = 60 * 1000;

      static bool rtcValid() {
        return s_rtc.magic == RtcMagic && s_rtc.magicInv == ~RtcMagic;
      }

      static void rtcReset() {
        memset(&s_rtc, 0, sizeof(s_rtc));
        s_rtc.magic = RtcMagic;
        s_rtc.magicInv = ~RtcMagic;
        s_rtc.slot = 0xFF;
        s_rtc.checkpoint = (uint8_t)CrashGuard::Checkpoint::Boot;
      }

      static bool epochLooksValid(int64_t sec) {
        // Any reasonably recent time (>= 2021-01-01) is considered valid.
        return sec >= 1609459200;
      }

      static int slotIndexFromPartition(const esp_partition_t* p) {
        if (!p)
          return -1;
        if (p->type != ESP_PARTITION_TYPE_APP)
          return -1;
        if (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
          return 0;
        if (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1)
          return 1;
        return -1;
      }

      static std::string partitionId(const esp_partition_t* p) {
        if (!p)
          return std::string();
        char buf[64];
        snprintf(buf, sizeof(buf), "%s:%d@0x%lx", p->label, (int)p->subtype,
                 (unsigned long)p->address);
        return std::string(buf);
      }

      static void appDescToJson(JsonObject node, const esp_app_desc_t& d) {
        // esp_app_desc_t strings are null-terminated.
        json::to(node, "proj", d.project_name);
        json::to(node, "ver", d.version);
        // Expose build time for humans; don't treat it as an ordering key.
        json::to(node, "date", d.date);
        json::to(node, "time", d.time);

        // ELF SHA256 is a stable identifier for a particular build.
        // Provide as hex so the UI can compare across slots.
        json::to(node, "sha256",
                 hex_encode(d.app_elf_sha256, sizeof(d.app_elf_sha256)));
      }

      struct PartitionOtaMeta {
        bool present = false;
        bool haveState = false;
        esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
        bool failed = false;
      };

      static PartitionOtaMeta partitionOtaInfoToJson(JsonObject node,
                                                     const esp_partition_t* p) {
        PartitionOtaMeta meta;
        if (!p)
          return meta;
        meta.present = true;
        json::to(node, "label", p->label);

        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(p, &desc) == ESP_OK) {
          appDescToJson(node["app"].to<JsonObject>(), desc);
        }

        // OTA image state is available only when rollback is enabled.
        // If unsupported, we simply omit the state fields.
        esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
        if (esp_ota_get_state_partition(p, &st) == ESP_OK) {
          meta.haveState = true;
          meta.state = st;
          meta.failed =
              (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED);
          json::to(node, "otaState", (int)st);
        }

        return meta;
      }

      static CrashGuard::OtaRole computeOtaRole(int slot, bool failedThis,
                                                bool failedOther,
                                                int runningSlot, int bootSlot) {
        if (failedThis)
          return CrashGuard::OtaRole::Failed;
        // If the other slot is known-bad and we're currently running this one,
        // treat the current one as a fallback.
        if (failedOther && runningSlot == slot)
          return CrashGuard::OtaRole::Fallback;
        // Preferred is whatever the OTA data says should boot next.
        if (bootSlot == slot)
          return CrashGuard::OtaRole::Preferred;
        return CrashGuard::OtaRole::Fallback;
      }

      static bool isFirmwareLikeReset(esp_reset_reason_t rr) {
        switch (rr) {
          case ESP_RST_PANIC:
          case ESP_RST_INT_WDT:
          case ESP_RST_TASK_WDT:
          case ESP_RST_WDT:
          case ESP_RST_BROWNOUT:  // treat separately elsewhere
          case ESP_RST_SDIO:
          case ESP_RST_DEEPSLEEP:
          case ESP_RST_EXT:
          case ESP_RST_SW:
          case ESP_RST_POWERON:
          case ESP_RST_UNKNOWN:
          default:
            break;
        }
        // Conservative: only consider these as clearly firmware-related.
        switch (rr) {
          case ESP_RST_PANIC:
          case ESP_RST_INT_WDT:
          case ESP_RST_TASK_WDT:
          case ESP_RST_WDT:
            return true;
          default:
            return false;
        }
      }

      static bool isPowerLikeReset(esp_reset_reason_t rr) {
        switch (rr) {
          case ESP_RST_BROWNOUT:
          case ESP_RST_POWERON:
            return true;
          default:
            return false;
        }
      }

      static std::string makeCrashSig(esp_reset_reason_t rr,
                                      CrashGuard::Checkpoint cp,
                                      const std::string& coredumpUid) {
        if (!coredumpUid.empty())
          return std::string("cd:") + coredumpUid;
        char buf[64];
        snprintf(buf, sizeof(buf), "rr:%d:cp:%d", (int)rr, (int)cp);
        return std::string(buf);
      }

      static void slotFromNvs(const NvsSlotState& src, CrashGuard::Slot& dst) {
        dst.strike = src.strike;
        dst.crashes = src.crashes;
        dst.brownouts = src.brownouts;
        dst.consecutiveEarly = src.consecutiveEarly;
        dst.lastGoodUptimeMs = src.lastGoodUptimeMs;
        dst.lastCrashSig = src.lastCrashSig;
      }

      static void slotToNvs(const CrashGuard::Slot& src, NvsSlotState& dst) {
        dst.strike = src.strike;
        dst.crashes = src.crashes;
        dst.brownouts = src.brownouts;
        dst.consecutiveEarly = src.consecutiveEarly;
        dst.lastGoodUptimeMs = src.lastGoodUptimeMs;
        memset(dst.lastCrashSig, 0, sizeof(dst.lastCrashSig));
        strncpy(dst.lastCrashSig, src.lastCrashSig.c_str(),
                sizeof(dst.lastCrashSig) - 1);
      }

      static bool nvsRead(NvsState& out) {
        memset(&out, 0, sizeof(out));
        out.version = NvsVersion;

        nvs_handle_t h;
        if (nvs_open(NvsNamespace, NVS_READONLY, &h) != ESP_OK)
          return false;
        size_t len = sizeof(out);
        esp_err_t r = nvs_get_blob(h, NvsKey, &out, &len);
        nvs_close(h);
        if (r != ESP_OK)
          return false;
        if (len != sizeof(out) || out.version != NvsVersion)
          return false;
        return true;
      }

      static bool nvsWrite(const NvsState& st) {
        nvs_handle_t h;
        if (nvs_open(NvsNamespace, NVS_READWRITE, &h) != ESP_OK)
          return false;
        esp_err_t r = nvs_set_blob(h, NvsKey, &st, sizeof(st));
        if (r == ESP_OK)
          r = nvs_commit(h);
        nvs_close(h);
        return r == ESP_OK;
      }

      static esp_err_t setBootPartitionOtherSlot(const esp_partition_t* running,
                                                 const esp_partition_t** chosen,
                                                 int* chosenSlot) {
        if (chosen)
          *chosen = nullptr;
        if (chosenSlot)
          *chosenSlot = -1;
        if (!running)
          return ESP_ERR_INVALID_ARG;
        int cur = slotIndexFromPartition(running);
        if (cur < 0)
          return ESP_ERR_INVALID_STATE;

        const esp_partition_t* other =
            esp_ota_get_next_update_partition(running);
        int otherSlot = slotIndexFromPartition(other);
        if (!other || otherSlot < 0 || otherSlot == cur)
          return ESP_ERR_NOT_FOUND;

        if (chosen)
          *chosen = other;
        if (chosenSlot)
          *chosenSlot = otherSlot;
        return esp_ota_set_boot_partition(other);
      }

    }  // namespace

    CrashGuard::CrashGuard() {
      CoreDump::instance();
      _bootMs = millis();
      onBoot();
    }

    void CrashGuard::handleEvent(Event& ev) {
      if (EventInit::is(ev, 0)) {
        _fsReady = true;
        setCheckpoint(Checkpoint::Init0);
        loadStateFromFs();
        if (_havePendingPrevRun) {
          _state.history.insert(_state.history.begin(), _pendingPrevRun);
          if (_state.history.size() > 16)
            _state.history.resize(16);
          _state.last = _pendingPrevRun;
          _havePendingPrevRun = false;
          saveStateToFs();
        }
      } else if (EventInited::is(ev)) {
        setCheckpoint(Checkpoint::Inited);
      } else if (EventPeriodic::is(ev)) {
        onPeriodic();
      } else {
        DoneReason reason;
        if (EventDone::is(ev, &reason))
          onDone(reason);
      }
    }

    bool CrashGuard::handleRequest(Request& req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("switchOta")) {
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* chosen = nullptr;
        int chosenSlot = -1;
        esp_err_t r = setBootPartitionOtherSlot(running, &chosen, &chosenSlot);
        if (r == ESP_OK) {
          req.respond();
          // Apply the new boot partition choice.
          // Use App::restart() to publish EventDone so CrashGuard can mark this
          // reset as planned.
          App::instance().restart();
        } else {
          req.respond(r);
        }
        return true;
      }
      return false;
    }

    JsonDocument* CrashGuard::getInfo(RequestContext& ctx) {
      (void)ctx;
      auto doc = new JsonDocument();
      auto root = doc->to<JsonObject>();

      json::to(root, "boot", _state.bootCount);
      json::to(root, "lastSwitch", _state.lastSwitchBoot);

      // OTA metadata and derived per-slot role classification.
      // This is stable for the duration of the boot.
      auto ota = root["ota"].to<JsonArray>();
      const esp_partition_t* p0 = esp_partition_find_first(
          ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
      const esp_partition_t* p1 = esp_partition_find_first(
          ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);

      const esp_partition_t* running = esp_ota_get_running_partition();
      const int curSlot = slotIndexFromPartition(running);

      const esp_partition_t* boot = esp_ota_get_boot_partition();
      int bootSlot = slotIndexFromPartition(boot);
      if (bootSlot < 0)
        bootSlot = curSlot;

      auto ota0 = ota.add<JsonObject>();
      auto meta0 = partitionOtaInfoToJson(ota0, p0);

      auto ota1 = ota.add<JsonObject>();
      auto meta1 = partitionOtaInfoToJson(ota1, p1);

      const auto role0 =
          computeOtaRole(0, meta0.failed, meta1.failed, curSlot, bootSlot);
      const auto role1 =
          computeOtaRole(1, meta1.failed, meta0.failed, curSlot, bootSlot);
      json::to(ota0, "role", (int)role0);
      json::to(ota1, "role", (int)role1);

      // Core dump presence/identity is stable within a boot.
      auto cd = root["coredump"].to<JsonObject>();
      const std::string cdUid = CoreDump::instance().uid();
      if (!cdUid.empty()) {
        json::to(cd, "uid", cdUid);
        int cdSlot = -1;
        if (_state.last.bootCount && _state.last.coredumpUid == cdUid)
          cdSlot = (int)_state.last.runningSlot;
        if (cdSlot < 0) {
          for (auto& r : _state.history) {
            if (r.coredumpUid == cdUid) {
              cdSlot = (int)r.runningSlot;
              break;
            }
          }
        }
        if (cdSlot >= 0)
          json::to(cd, "slot", cdSlot);
      }

      // Last finalized run + ring buffer of recent finalized runs.
      if (_state.last.bootCount)
        _state.last.toJson(root["last"].to<JsonObject>());

      auto h = root["history"].to<JsonArray>();
      for (auto& r : _state.history) r.toJson(h.add<JsonObject>());

      return doc;
    }

    JsonDocument* CrashGuard::getState(RequestContext& ctx) {
      (void)ctx;
      auto doc = new JsonDocument();
      auto root = doc->to<JsonObject>();

      const uint32_t now = millis();
      const uint32_t uptime = now - _bootMs;
      json::to(root, "uptime", (int)uptime);

      const esp_partition_t* running = esp_ota_get_running_partition();
      const int curSlot = slotIndexFromPartition(running);
      json::to(root, "slot", curSlot);

      // Live per-slot health counters (may change within a boot).
      auto ota = root["ota"].to<JsonArray>();
      _state.slot0.toJson(ota.add<JsonObject>());
      _state.slot1.toJson(ota.add<JsonObject>());

      // Live RTC ledger snapshot (cheap, survives unexpected resets)
      auto rtc = root["rtc"].to<JsonObject>();
      json::to(rtc, "valid", rtcValid());
      json::to(rtc, "boot", (int)s_rtc.bootCount);
      json::to(rtc, "slot", (int)s_rtc.slot);
      json::to(rtc, "cp", (int)s_rtc.checkpoint);
      json::to(rtc, "inProgress", (int)s_rtc.inProgress);
      json::to(rtc, "planned", (int)s_rtc.plannedReset);
      json::to(rtc, "uptime", (int)s_rtc.lastUptimeMs);
      if (s_rtc.lastCrashSig[0])
        json::to(rtc, "sig", s_rtc.lastCrashSig);

      return doc;
    }

    void CrashGuard::onBoot() {
      if (!rtcValid())
        rtcReset();

      const esp_partition_t* running = esp_ota_get_running_partition();
      int curSlot = slotIndexFromPartition(running);

      NvsState nvs;
      if (!nvsRead(nvs)) {
        memset(&nvs, 0, sizeof(nvs));
        nvs.version = NvsVersion;
      }

      // Finalize previous run based on reset reason of *this* boot.
      esp_reset_reason_t rr = esp_reset_reason();

      Run prev;
      bool havePrev = rtcValid() && (s_rtc.bootCount != 0);
      if (havePrev) {
        prev.bootCount = s_rtc.bootCount;
        prev.resetReason = (uint32_t)rr;
        prev.uptimeMs = s_rtc.lastUptimeMs;
        prev.ts = s_rtc.firstEpochSec;
        prev.checkpoint = (Checkpoint)s_rtc.checkpoint;
        prev.unexpectedReset = s_rtc.inProgress != 0;
        prev.plannedReset = s_rtc.plannedReset != 0;
        prev.crashSig = s_rtc.lastCrashSig;
        // Attribute to the slot recorded by the previous run if available.
        int prevSlot = (s_rtc.slot <= 1) ? (int)s_rtc.slot : curSlot;
        prev.runningSlot = (prevSlot == 0 || prevSlot == 1) ? (int8_t)prevSlot
                                                            : (int8_t)-1;

        if (CoreDump::instance().isAvailable()) {
          // Best-effort: use UID as a stable signature.
          // (UID is already stored in CoreDump, but not exposed; we rely on
          // makeCrashSig from the UID stored into rtc at runtime.)
        }

        _pendingPrevRun = prev;
        _havePendingPrevRun = true;
      }

      // Bump boot counter.
      nvs.bootCount++;
      _state.bootCount = nvs.bootCount;
      _state.lastSwitchBoot = nvs.lastSwitchBoot;
      slotFromNvs(nvs.slot[0], _state.slot0);
      slotFromNvs(nvs.slot[1], _state.slot1);

      // Update crash signature for the *previous* run (now that coredump UID is
      // known). Store it into RTC immediately for clustering.
      const std::string cdUid = CoreDump::instance().uid();
      if (_havePendingPrevRun && !cdUid.empty())
        _pendingPrevRun.coredumpUid = cdUid;

      // Decide whether previous reset should count as a crash.
      bool countsAsCrash = false;
      if (havePrev) {
        if (prev.plannedReset)
          countsAsCrash = false;
        else if (!prev.unexpectedReset)
          countsAsCrash = false;
        else
          countsAsCrash = true;
      }

      int affectedSlot =
          (havePrev && s_rtc.slot <= 1) ? (int)s_rtc.slot : curSlot;
      CrashGuard::Slot* slot = nullptr;
      if (affectedSlot == 0)
        slot = &_state.slot0;
      else if (affectedSlot == 1)
        slot = &_state.slot1;

      if (slot && countsAsCrash) {
        if (isPowerLikeReset(rr)) {
          slot->brownouts++;
        } else if (isFirmwareLikeReset(rr)) {
          slot->crashes++;

          // Strike scoring (conservative).
          int add = 1;
          if (prev.uptimeMs < 10 * 1000)
            add = 5;
          else if (prev.uptimeMs < 60 * 1000)
            add = 3;
          else if (prev.uptimeMs < 10 * 60 * 1000)
            add = 2;

          if (prev.uptimeMs < EarlyCrashMs)
            slot->consecutiveEarly++;
          else
            slot->consecutiveEarly = 0;

          // Same-signature amplification.
          if (!prev.crashSig.empty() && prev.crashSig == slot->lastCrashSig)
            add += 2;

          slot->strike += add;
          slot->lastCrashSig = prev.crashSig;
        }
      }

      // Consider switching partitions only for repeated early firmware-like
      // crashes.
      bool shouldSwitch = false;
      if (slot && countsAsCrash && isFirmwareLikeReset(rr) &&
          affectedSlot == curSlot && affectedSlot >= 0) {
        if (slot->consecutiveEarly >= 3 || slot->strike >= 10)
          shouldSwitch = true;
      }

      // Anti ping-pong: never switch more than once in 2 boots.
      if (shouldSwitch && (nvs.bootCount - nvs.lastSwitchBoot) < 2)
        shouldSwitch = false;

      if (shouldSwitch) {
        const esp_partition_t* chosen = nullptr;
        int chosenSlot = -1;
        auto err = setBootPartitionOtherSlot(running, &chosen, &chosenSlot);
        if (err == ESP_OK) {
          logW("CrashGuard: switching from %s to %s",
               partitionId(running).c_str(), partitionId(chosen).c_str());
          nvs.lastSwitchBoot = nvs.bootCount;
          _state.lastSwitchBoot = nvs.lastSwitchBoot;

          // Persist NVS before restart.
          slotToNvs(_state.slot0, nvs.slot[0]);
          slotToNvs(_state.slot1, nvs.slot[1]);
          nvsWrite(nvs);

          // Mark planned reset so the next boot doesn't treat it as a crash.
          rtcReset();
          s_rtc.bootCount = nvs.bootCount;
          s_rtc.slot = (uint8_t)chosenSlot;
          s_rtc.inProgress = 0;
          s_rtc.plannedReset = 1;
          strncpy(s_rtc.lastCrashSig, "switch", sizeof(s_rtc.lastCrashSig) - 1);
          esp_restart();
        } else {
          logW("CrashGuard: requested switch but failed: %d", (int)err);
        }
      }

      // Start current run in RTC.
      rtcReset();
      s_rtc.bootCount = nvs.bootCount;
      s_rtc.slot = (curSlot >= 0) ? (uint8_t)curSlot : 0xFF;
      s_rtc.checkpoint = (uint8_t)Checkpoint::Boot;
      s_rtc.inProgress = 1;
      s_rtc.plannedReset = 0;
      s_rtc.lastUptimeMs = 0;
      memset(s_rtc.lastCrashSig, 0, sizeof(s_rtc.lastCrashSig));

      // Initialize RTC signature for early clustering.
      {
        std::string sig = makeCrashSig(esp_reset_reason(), Checkpoint::Boot,
                                       CoreDump::instance().uid());
        strncpy(s_rtc.lastCrashSig, sig.c_str(),
                sizeof(s_rtc.lastCrashSig) - 1);
      }

      // Also mirror into in-memory state.
      _state.bootCount = nvs.bootCount;
      _state.history.reserve(16);

      // Write back NVS (bootCount + counters changes).
      slotToNvs(_state.slot0, nvs.slot[0]);
      slotToNvs(_state.slot1, nvs.slot[1]);
      nvsWrite(nvs);
    }

    void CrashGuard::setCheckpoint(Checkpoint cp) {
      s_rtc.checkpoint = (uint8_t)cp;

      // Record the earliest valid wall-clock time for this run.
      if (s_rtc.firstEpochSec == 0) {
        const time_t now = time(nullptr);
        const int64_t sec = (int64_t)now;
        if (epochLooksValid(sec))
          s_rtc.firstEpochSec = sec;
      }

      // Update RTC signature (cheap).
      auto rr = esp_reset_reason();
      std::string sig = makeCrashSig(rr, cp, CoreDump::instance().uid());
      memset(s_rtc.lastCrashSig, 0, sizeof(s_rtc.lastCrashSig));
      strncpy(s_rtc.lastCrashSig, sig.c_str(), sizeof(s_rtc.lastCrashSig) - 1);
    }

    void CrashGuard::onPeriodic() {
      uint32_t now = millis();
      uint32_t up = now - _bootMs;
      s_rtc.lastUptimeMs = up;

      int curSlot = slotIndexFromPartition(esp_ota_get_running_partition());
      CrashGuard::Slot* slot = nullptr;
      if (curSlot == 0)
        slot = &_state.slot0;
      else if (curSlot == 1)
        slot = &_state.slot1;

      if (!_savedStableThisBoot && up >= StableMs && slot) {
        // Forgive strikes on a stable run.
        slot->strike = std::max<int32_t>(0, slot->strike - 5);
        slot->consecutiveEarly = 0;
        slot->lastGoodUptimeMs = up;
        _savedStableThisBoot = true;
        setCheckpoint(Checkpoint::Stable);

        NvsState nvs;
        if (nvsRead(nvs)) {
          nvs.bootCount = _state.bootCount;
          nvs.lastSwitchBoot = _state.lastSwitchBoot;
          slotToNvs(_state.slot0, nvs.slot[0]);
          slotToNvs(_state.slot1, nvs.slot[1]);
          nvsWrite(nvs);
        }
      }

      if (_fsReady && (now - _lastFsSaveMs) >= FsSavePeriodMs) {
        saveStateToFs();
        _lastFsSaveMs = now;
      }
    }

    void CrashGuard::onDone(DoneReason reason) {
      (void)reason;
      s_rtc.inProgress = 0;
      s_rtc.plannedReset = 1;
      s_rtc.checkpoint = (uint8_t)Checkpoint::Done;
      if (_fsReady)
        saveStateToFs();
    }

    void CrashGuard::loadStateFromFs() {
      uint32_t currentBoot = _state.bootCount;
      uint32_t currentLastSwitch = _state.lastSwitchBoot;
      auto doc = json::loadFromFile(FilePath);
      if (!doc)
        return;
      bool changed = false;
      _state.fromJson(doc->as<JsonObjectConst>(), changed);
      // Never trust FS for counters that are maintained in NVS.
      _state.bootCount = currentBoot;
      _state.lastSwitchBoot = currentLastSwitch;
    }

    void CrashGuard::saveStateToFs() {
      auto doc = new JsonDocument();
      _state.toJson(doc->to<JsonObject>());
      json::saveToFile(FilePath, doc->as<JsonVariantConst>());
      delete doc;
    }

  }  // namespace debug
}  // namespace esp32m
