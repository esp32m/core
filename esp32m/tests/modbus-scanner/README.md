# Modbus scanner regression tests

Run from the workspace root:

```
node core/esp32m/tests/modbus-scanner/run.mjs
node core/esp32m/tests/modbus-scanner/ui.mjs
```

The native runner uses Windows MinGW C++17 and ArduinoJson 7 from the workspace
greenhouse dependency. Override `CXX` and `ARDUINOJSON_INCLUDE` if necessary.
Production header/source are extracted unchanged; only the platform, requests
and master are faked. The fake master intentionally has no UART reconfiguration
methods. Tests never open a serial port, issue network requests or operate devices.

The native scheduler models separate queued/running/deleted task handles and
watchdog membership. It checks that initialization and reads of cached state
allocate no worker, allocation failures preserve retained state/configuration,
and completed workers release their response and watchdog before self-deletion.
It also submits a new job during final response publication and at the retirement
boundary, runs a replacement before its predecessor deletes itself, and rejects
overlapping active jobs. The platform deliberately provides no task notification
APIs, so a stale or recycled task handle cannot silently pass the suite.

The UI runner transpiles the actual TSX/types/helpers with workspace Babel and
uses deterministic hooks, time and backend responses. It verifies generic bus
and single-address scans, cancel/reload, stale status, input validation, confirmed
writes, correct FC16, and lost-response handling without retries. It also guards
against device-specific names and register descriptions appearing in the UI.
