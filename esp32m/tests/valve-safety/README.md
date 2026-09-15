# Valve fault and travel-limit regressions

From the workspace root:

```powershell
node core/esp32m/tests/valve-safety/run.mjs
```

The runner compiles the complete current production Valve declaration and
implementation unchanged, replacing only platform/device dependencies with host
doubles. It uses ArduinoJson 7 from greenhouse's managed components for actual
JSON command/state checks (`ARDUINOJSON_INCLUDE` can select another installation).
Set `CXX` to override the host C++17 compiler.

Fault injection covers asynchronous and synchronous 10-second travel deadlines,
repeated commands retaining the deadline, travel across the 32-bit millisecond
wrap boundary, sensor loss and contradictory limits, failures during starting
and stopping, paced persistent Off retries, latched errors after recovery,
explicit acknowledgement, absence of motion replay, UI/HA error propagation,
faulted-position persistence suppression, pin-direction retries and failed task
creation. A concurrent command runs while the worker is held inside its sensor
read to verify serialization. Another thread stops a blocking movement while
its caller is sleeping, verifying that the wait releases the valve mutex.

These tests validate software decisions and actual emitted command sequences.
They do not establish motor electrical state, I2C timing or watchdog behavior
on the ESP32. A firmware build and hardware observation remain necessary.
