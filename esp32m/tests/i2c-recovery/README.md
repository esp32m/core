# I2C recovery regression test

Run from the repository root with Node.js and a C++17 compiler:

```powershell
node core/esp32m/tests/i2c-recovery/run.mjs
```

The runner recognizes the workspace MinGW installation; `CXX` can select another
compiler. It compiles the actual production `master.hpp` and `src/bus/i2c.cpp`,
replacing only the ESP-IDF/base/logging dependencies with small host stubs. It
creates and removes its own temporary build directory. No hardware is accessed.

Fault injection checks timeout recovery, the per-device opt-in for repeating
`ESP_ERR_INVALID_RESPONSE`, nonretryable errors, retry limits (including zero and
negative input), reset failure propagation, and nonzero waits with a 100 Hz RTOS
tick. Concurrent calls hold SDK transfers/reset/probe/device add/remove at test
barriers and verify shared lock ownership, exclusion, and ordering through
recovery. Additional checks cover typed transfers and communication statistics.

This validates wrapper logic and synchronization with host threads. It does not
emulate the ESP32 peripheral, electrical faults, or ESP-IDF's recovery internals;
the firmware still needs rebuilding with the user's ESP-IDF 6.1 and field testing.
