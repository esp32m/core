# BFS Parameter Registry Tests

Run from the repository root:

```sh
node core/esp32m/tests/bfs-parameters/run.mjs
node core/esp32m/tests/bfs-parameters/catalog.mjs
```

The suite compiles the production descriptor header with a host C++17 compiler.
Set `CXX` when the compiler is not on PATH or at a known local MinGW path.

Coverage includes the 79-address configuration allowlist, decimal parameter
indices, excluded communication and command registers, documented raw ranges,
engineering scales, contradictory enum limits, and blocked reserved, action,
unknown-scale, model-dependent, and signed parameters. The protection words are
checked across all 65,536 raw values, including invalid values inside their
numeric ranges.

These tests do not validate live inter-register dependencies, Modbus transport,
or physical drive behavior. The driver performs current dependency reads before
any write; hardware validation is separate.

`catalog.mjs` compiles `export.cpp` against the same production registry and
checks the checked-in UI catalog for parity. Run it with `--write` after changing
the registry to regenerate `web-ui/esp32m/ui/src/dev/bfs/catalog.ts`. The exporter
uses ArduinoJson 7; set `ARDUINOJSON_INCLUDE` if the water-well managed component
is unavailable. No C++ source text parsing is used.
