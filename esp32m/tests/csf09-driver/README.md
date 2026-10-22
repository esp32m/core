# CSF09 host driver tests

Run from the repository root:

```sh
node core/esp32m/tests/csf09-driver/run.mjs
```

The runner compiles the actual `csf09.hpp` and `csf09.cpp` implementation
against ArduinoJson 7 and focused Device, Sensor, monotonic-clock and Modbus
doubles. It uses `CXX` when set, otherwise a local MinGW compiler or `g++` on
PATH. Set `ARDUINOJSON_INCLUDE` to an ArduinoJson 7 `src` directory if the
greenhouse managed component is unavailable. Build artifacts are temporary.

Sixteen scenario groups cover:

- Unseen state, failed first reads, sensor metadata and default/custom names.
- The manual's `315,650` example (`31.5%`, `15°C`), range endpoints, the
  public factory, configured slave address and read-only state snapshots.
- The reported regression where normal readings work but the ID dialog
  fails: configuration verification uses the same documented measurement
  registers, without trying to read the write-only address register.
- Whole-sample rejection for either register above 1000, unsigned invalid
  values, failed transport with a populated buffer, preserved previous
  values/acquisition timestamp, and recovery with a new valid sample.
- Stable absolute timestamps through stale readings and beyond 32-bit uptime,
  including a valid acquisition at monotonic zero and never-read `-1`.
- Starting only a stopped master, no restart/retune/delay for an active shared
  bus, start failures and standard unicast address bounds.
- A concurrent JSON snapshot waiting until both sensor values are published,
  preventing mixed readings from different samples.
- Strict address/baud validation before hardware access, broadcast rejection,
  local identity/pending restoration without I/O, and invalidating readings
  when a restored identity changes.
- Fixed 9600/RTU/no-parity bus guards and configuration reads that never start
  or retune the shared master.
- A single non-retried FC06 ID change, old/destination preflight, independent
  verification through normal readings at the new ID even after lost/invalid
  ACKs, and ignored writes.
- Destination occupancy detected through normal measurement reads; valid
  replies, impossible raw values and transport errors all block a write.
  Only a clean destination timeout permits the single FC06 transaction.
- Uncertain old/new identities retained in saved configuration and restored
  after a simulated driver restart; read-only resolution from init, poll or
  configuration refresh, with automatic resolution requesting persistence.
- Scheduler polling skipping a slow configuration mutex without waiting or
  issuing extra transactions.
- Explicit `connectAddress` selection rejecting invalid/mixed payloads,
  internal/broadcast/disabled/pending requests and incompatible/stopped buses
  before I/O, while accepting standard unicast ID bounds.
- Failed selection preserving the old address, values and sample timestamp;
  selecting the same address still verifies it without resetting its cached
  readings, and clears the previous communication error on success.
- Replacing a commissioned address-9 sensor with a factory address-1 unit:
  restoring saved ID 9 and opening settings never auto-select ID 1; explicit
  selection uses one read, then a separate physical change to ID 10 uses one
  FC06. Replacing/selecting another factory unit adds no physical write.
  The transient `connectAddress` operation is absent from saved config.

The fake transport rejects non-unicast addresses and undocumented accesses.
It allows FC03 at register `0x0000` with count 2 and single-register FC06
at `0x000F`. An FC03 read of `0x000F` explicitly fails the suite. Address
preflight, verification and recovery use non-retried measurement reads;
the address write also disables retry. These tests verify host
driver behavior, not ESP-IDF integration, RS485 timing/wiring, physical
calibration or the sensor's hardware behavior. Restoring an already-saved
pending configuration does not demonstrate persistence across abrupt power
loss before the framework completes its deferred configuration save.
