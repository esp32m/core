# SM10 driver tests

Run from the workspace root:

```sh
node core/esp32m/tests/sm10-driver/run.mjs
```

This compiles the complete production SM10 declaration and implementation,
unchanged apart from platform includes/time bindings. It uses the actual
protocol codec and ArduinoJson 7. Platform services, the Device/Sensor base
classes, and the serial meter are test doubles. Set `CXX` and
`ARDUINOJSON_INCLUDE` if the default compiler/JSON paths are unavailable.
For the deployed scratch JSON parser, set `ARDUINOJSON_INCLUDE` to
`projects/scratch/managed_components/bblanchon__arduinojson/src` under the
workspace root before running. Decimal-total regressions must pass through
actual `deserializeJson`, because direct C++ double assignment misses the
parser's float narrowing behavior.

The simulated meter processes real transmitted bytes and changes address,
baud, total or calendar only in response to the corresponding application
packet. It can lose acknowledgments, return a success acknowledgment without
applying a write, or disappear during verification. The tests check:

- Whole L/h on the wire converts to m³/h exactly once, including zero,
  1 L/h, 2362 L/h and 12345 L/h (2026-09-29 scaling regression).
- Invalid RTC sync only when host time is valid, independent readback, valid
  RTC preservation, and no writes after failed reads.
- Periodic detection of an independent meter restart and 64-bit deadlines
  across the 49-day millisecond boundary.
- Local saved/pending configuration restoration without serial I/O.
- Rejection of invalid or unconfirmed writes and broadcast total changes.
- History fetched only on request, with all hourly entries in wire order.
- Idle detection before coupled configuration/total writes.
- One transmission per requested write, followed by readback even with a lost
  acknowledgment; rejected success acknowledgments when values did not change.
- Preservation of opaque address bytes and coarse total during ID/baud changes,
  and read-only recovery of uncertain outcomes after reconnect or ESP restart.
- Preservation of existing alternate 1200-baud selector bytes during recovery.

No serial port, ESP32, debug probe or physical meter is accessed. These tests
do not qualify the hardware UART transition or untested meter configuration
writes; those remain hardware acceptance tasks.
