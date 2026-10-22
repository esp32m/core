# OpenTherm thermostat host tests

From the workspace root:

```powershell
node core/esp32m/tests/opentherm-thermostat/run.mjs
```

The runner compiles unchanged production OpenTherm declarations and control,
scheduler, response-decoder and transmission bodies with a fake transport and
clock. It also compiles the production master device's direct Actuator methods.
The test passes that device itself as an Actuator; no adapter is constructed.
Only ESP-IDF task, logging and Device/framework glue are replaced. Set `CXX` or
`ARDUINOJSON_INCLUDE` if the local compiler/ArduinoJson paths differ.

Coverage includes model-only transient flow targets, unsupported Start/Stop,
invalid outputs, preserved configured targets/CH/DHW flags, unchanged DHW and
other message frames, CH changes between computation and transmission, retry
reconciliation, automatic gate resumption, lease expiry, manual takeover
revision checks, DHW-only action and honest response freshness. A blocked fake
send verifies that device CH changes serialize behind a frame already in flight.

These are deterministic host/transport checks. They do not qualify boiler
response to a zero flow target, thermal regulation, communications failures,
manufacturer behavior or physical OpenTherm operation.
