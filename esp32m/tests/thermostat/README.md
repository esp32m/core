# Thermostat policy and sample regression tests

`node tests/thermostat/run.mjs` compiles the platform-independent production
controller with a deterministic clock. Eighteen groups cover device-owned CH
gate suspension/resumption and apply races, bounded flow regulation/no-demand,
leases, freshness, lifecycle waits, low-output dwell, native standby policy,
unsupported combinations, high-temperature/explicit Off, fault/manual latches,
native target updates and no replay after uncertain Start.

`node tests/thermostat/sensor.mjs` compiles the production Sensor declaration
against small framework doubles. Stable successful readings refresh acquisition
time without false change events; cached producers retain actual sample time;
nonfinite/nonnumeric/reset/disabled/generation-invalidated values are rejected.
Known failed-poll generation changes are also checked by tests/sensor-polling.

`compile.py` uses an ESP-IDF compile database to build isolated objects under
`.build-compile/`. It does not overwrite application firmware/build metadata.
It also compiles a public-header check proving all three device classes derive
from Actuator and support direct useThermostat attachment. Wrapper lifecycle,
borrowed/owned destruction and duplicate attachment checks are in
tests/thermostat-device.
Its 13 groups also verify same-name duplicate request isolation, LF-compatible
attachment hooks, manual-revision release guards, preserved default names and
null-actuator diagnostics.
MinGW/GCC and ArduinoJson 7 are used by host tests; CXX and ARDUINOJSON_INCLUDE
override their local defaults. No test operates live equipment.
