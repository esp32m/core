# HSM-WT10 host driver tests

Run from the repository root:

```sh
node core/esp32m/tests/hsmwt10-driver/run.mjs
```

The runner compiles the actual `hsmwt10.hpp` and `hsmwt10.cpp` implementation
against ArduinoJson 7 with small fake Device, BinarySensor and Modbus APIs.
It uses `CXX` when set, otherwise a local MinGW compiler or `g++` on PATH.
Set `ARDUINOJSON_INCLUDE` to the ArduinoJson `src` directory if the existing
greenhouse managed component is unavailable. The executable is built in a
temporary directory and removed afterwards.

The fake RTU sensor records every request and enforces the manual's single
register FC03 reads and a single, non-retried FC06 write for each mutation.
The cases cover:

- A simulated slave requiring a quiet interval after other bus traffic;
  telemetry and settings reads request the transport's reserved interval.
  This models the timing hypothesis, not measured sensor firmware behavior.
- Normal and alarm states, raw analog data, invalid alarm values, and loss of
  communication clearing both JSON validity and both sensor states; stale
  values and timestamps beyond 32-bit millisecond uptime.
- Strict configuration type/range validation before bus access, and internal
  configuration restore without hardware writes.
- Verified address changes with normal, lost, or invalid/new-address ACKs;
  the six documented baud-rate codes, including no-op changes.
- Occupied destination rejection, ambiguous probe responses, and restoring
  the master baud rate after an unsuccessful destination probe.
- Local bus startup or baud-switch timeouts during destination preflight and
  verification, including preventing a preflight timeout from authorizing a
  write and recovering a failed post-write verification read-only.
- Uncertain writes retained in persisted pending configuration, prevention
  of repeated writes, and read-only recovery after polling or restart.
- Restoring nondefault and pending tuples despite transient local UART
  failures, both before and after the master applies the baud-rate change.
- A concurrent scheduler poll skipping a configuration mutex without waiting
  for the slow transaction or issuing additional RTU requests.
- Keeping a successfully verified address if a following baud change fails;
  sensitivity and alarm-delay read-back, no-op settings, and read errors.

These tests validate driver control flow and JSON behavior. They do not
validate ESP-IDF integration, RS485 wiring, RTU timing, physical leak detection,
or the purchased sensor's firmware behavior.
