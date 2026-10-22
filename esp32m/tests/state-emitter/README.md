# State Emitter Regression Tests

From the parent workspace:

```powershell
node core/esp32m/tests/state-emitter/run.mjs
```

The runner compiles the unchanged production `StateEmitter` declaration,
event handler and task loop from `device.hpp` / `src/dev/device.cpp` with a
fake clock, component registry, FreeRTOS waits and output sink. It requires
Node.js and a C++17 compiler with 32-bit `unsigned long`, matching ESP32;
the local Windows MinGW paths are detected, or set `CXX` explicitly.

Six scenario groups cover:

- Stable 1.9 A sensor refreshed every second despite another sensor changing
  every 100 ms, with immediate/coalesced change delivery and no duplicate
  sample when a queued change coincides with a full snapshot.
- Idle sensors, empty early wake-ups, watchdog wake-ups during long intervals,
  and positive sub-tick waits at a 100 Hz RTOS tick rate.
- On-change-only and periodic-only modes, filtered/disabled components,
  including a component disabled after being queued.
- A periodic deadline across 32-bit `millis()` rollover, and slow output
  completion without catch-up bursts.
- Nonpositive interval clamping and watchdog seconds-to-milliseconds bounds.
- The public virtual `nextTime()` override honored by `shouldEmit()`.

The stable/busy test failed against the original production code with
`frequent changes starved the stable sensor's periodic refresh`, then passed
after fixing the scheduling timestamp. The test does not emulate MQTT delivery,
broker outages, InfluxDB ingestion, or physical sensor operation. No device or
network access is performed. Temporary generated source/binaries are removed
after each run.
