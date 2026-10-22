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

The UI runner transpiles the actual TSX/types/helpers with workspace Babel and
uses deterministic hooks, time and backend responses. It verifies presets are
read-only, single-address requests, cancel/reload, stale status, input validation,
confirmed writes, correct FC16, and lost-response handling without retries.
