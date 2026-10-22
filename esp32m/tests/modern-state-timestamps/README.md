# Modern state timestamp tests

Run `node core/esp32m/tests/modern-state-timestamps/run.mjs`.
The runner compiles unchanged production Fan snapshot/polling, Flomarvel snapshot,
and RS485 snapshot serializers with real ArduinoJson and RS485 statistics.
Platform and unrelated physical driver operations are replaced with test doubles.

Checks cover exact independent source timestamps beyond 32-bit millis rollover,
never-read sentinels, unchanged reads, failed-observation timestamp retention,
and preserved backend freshness/RS485 rate and latency semantics. Driver suites
separately cover SM10, HSMWT10, BFS, CDHX and thermostat state serialization.
No physical device, firmware build or network access is used.
