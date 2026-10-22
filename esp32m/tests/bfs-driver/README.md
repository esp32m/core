# BFS Driver Regression Tests

Run `node core/esp32m/tests/bfs-driver/run.mjs` from the parent workspace.
The runner compiles the production BFS declaration and implementation with
ArduinoJson and fake device, task, clock, and Modbus services.

Async response publishing also uses the production `ui::makeResponse`
serializer, including its JSON copy and string growth. Compact bulk, single,
error and maximum-width value replies must stay below 4 KB on the wire and
8 KB string capacity. This guards against the original metadata-heavy reply
that aborted water-well 1.0.9 on a failed 30,721-byte string allocation. It is
a payload-budget regression, not a simulation of ESP32 heap fragmentation.

Coverage includes independent stale telemetry, 64-bit sample ages, display-only
state, deferred on-demand reads, bounded offline scans, control during a dialog
read, protected registers, dynamic limits, stale edit baselines, no-op writes,
lost/misleading acknowledgements, source-selection STOP sequencing, explicit
RUN confirmation, broadcast rejection, occupied communication destinations,
address/baud migration, pending settings, and recovery without another write.
RUN must send exactly 0x0012 and STOP 0x0001. F2-04 accepts raw integers 0..3,
rejects other values or running-state writes, sends once and reads back.
Communication diagnostics tests distinguish current-register mismatch,
inconclusive/occupied destination, running-state rejection, ignored write and
successful migration. Settings probes are read-only; persistence stays local.

The 0x0017 pressure target has RUN-enabled, live-limit, mode/fault and d-08
verification tests, including lost acknowledgement without write retries.
Cache coverage checks zero-I/O cache access, deferred changed-only persistence,
restoration without hardware writes, disabled access, stale-baseline rejection,
tuple identity and interactive injection rejection. A synchronous next-read
callback on response publication guards incremental refresh against a busy-slot
race; this test fails if the read slot is only released after publication.

Shared-bus dialog regressions require the production driver to request its
12 ms pre-transmission quiet interval inside the master transaction. The fake
bus rejects unguarded transactions when competing traffic is enabled. This
checks the driver contract; `tests/modbus-raw` separately checks the real
master's lock and delay behavior. It does not establish a physical timing
threshold for BFS. Aborted scans also distinguish three attempted timeouts
from skipped registers, expose the abort reason, and retain the payload budget.

An optional output path generates the UI's sample fixture from the real backend
JSON serialization:

```text
node core/esp32m/tests/bfs-driver/run.mjs core/esp32m/tests/bfs-ui/fixture.json
```

The fixture uses simulated values, including representative historical fault
values. It is not a live read or a recommended configuration. These tests do
not validate physical communication timing, converter writes, motor operation,
or FreeRTOS scheduling under load.
