# Wi-Fi roaming regressions

Run from the workspace root:

```powershell
node core/esp32m/tests/wifi-roaming/run.mjs
```

The runner compiles the production `RoamingPolicy` header and the unchanged
`Wifi::tryConnect()`, `checkRoaming()`, `finishRoamingScan()` and `roamTo()`
methods. Platform doubles supply radio/event/connection outcomes; the tests
do not copy the production policy, worker, selection or migration algorithms.
Set `CXX` to override the host C++17 compiler.

Coverage includes weak-signal cooldown, disabled roaming, healthy connections,
neighbor replies and missing replies, BTM failure and timeout fallback,
unavailable scans, failed-roam association recovery, timer rollover, candidate
signal hysteresis, pinned BSSID priority, ordinary fallback, and explicit UI
connection priority and stale failed intent cleanup. A saved profile's failure
counter does not skip its pin. Worker checks count actual RSSI rearming and
RRM/BTM/scan calls, defer to UI scan ownership, cancel background work during
OTA/manual requests, reject wrong SSIDs/security and handle allocation/API
failures. Migration checks preserve authenticated passphrases and 64-byte PSKs,
avoid persisting temporary pins, restore prior settings after failure, and let
a newly queued manual connection win. Layer-2 association retains a bounded
grace period for DHCP before ordinary reconnection resumes.

These host checks establish software decisions, not radio interoperability or
successful association/DHCP. The ESP-IDF firmware build and bounded live device
observation remain necessary.
