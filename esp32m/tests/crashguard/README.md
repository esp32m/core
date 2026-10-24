# CrashGuard recovery visibility host checks

Run from the workspace root:

```powershell
node core/esp32m/tests/crashguard/run.mjs
```

The runner compiles the complete production CrashGuard header and source with
ArduinoJson 7. ESP-IDF partitions, descriptors, boot selection, restart, RTC,
NVS transactions, framework events and the clock are doubled. All scoring,
thresholds, recovery control flow, persistence and JSON serialization run from
production bodies. Temporary compiler output is created under the OS temporary
directory and removed after the run. No device, serial port or network is used.

Checks cover:

- The existing score threshold, independent consecutive-early threshold,
  anti-ping-pong limit, planned resets, brownouts, absent destination and
  unchanged version-1 score blob.
- Intent commitment before boot selection; source/target identities including
  equal version strings with different SHA values; original causal evidence.
- Actual destination boot confirmation; fallback power cycles; failed uploads;
  replacement and exact rejected-build retries; resolution at the existing
  ten-minute stable checkpoint; stable fallback retention.
- Manual switches, canceled selection, failed reverse recovery preserving a
  prior confirmed incident, and historical resolved-event retention.
- Intent/success-marker/confirmation/cancellation/acceptance persistence
  failures, invalid schemas and identities, and power cuts around selection.
- Compact subscribed-state content remaining byte-identical as time advances,
  no periodic descriptor reads, no periodic recovery notifications, and full
  diagnostic state/info compatibility.

The new persistence uses two separate version-1 blobs in namespace
`crashguard`: `recovery_intent` stages a proposed automatic switch; `recovery`
retains the last confirmed incident and its eventual resolution. The existing
`state` key and its recovery decisions are unchanged. Failed selection never
replaces a previous confirmed event. An already committed confirmation or
resolution wins over its matching stale intent if canceling that intent failed.

A staged intent needs both an actual matching destination slot/SHA and success
evidence from its committed selection marker, exact `lastSwitchBoot`, or the
existing planned-switch RTC ledger. A power cut immediately after OTA metadata
selection but before any success evidence is durable leaves an inactive pending
intent. This deliberately avoids claiming completed recovery from an ambiguous
intent. A valid RTC `Done` from the source cancels a stale intent during a later
planned/manual transition, even if its earlier cancellation write failed.
When cancellation itself cannot persist and RTC evidence is then lost, the
remaining selected intent cannot distinguish all later manual transitions;
ordinary NVS durability is required for that guarantee.

Recovery-record write failures are logged and never prevent boot selection or
restart. A failed resolution write keeps the warning active; a later boot can
retry after reaching the same stable checkpoint. These are host checks, not
physical device or bootloader acceptance tests.
