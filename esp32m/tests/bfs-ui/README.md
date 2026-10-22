# BFS UI checks

Run from the workspace root (`C:\work\esp32\esp32m`):

```powershell
node core/esp32m/tests/bfs-ui/run.mjs
node core/esp32m/tests/bfs-parameters/catalog.mjs
node core/esp32m/tests/bfs-ui/bundle.mjs
node core/esp32m/tests/bfs-ui/browser.mjs
```

- `run.mjs` executes the actual component, parameter conversion helpers and shared
  MessageBox with deterministic React and backend doubles. It checks confirmation,
  duplicate actions, unverified outcomes, stale telemetry, on-demand reads,
  protected parameters, validation and one-register write/read-back behavior.
  F2-04 has a dedicated raw-integer editor for 0..3, with no invented labels;
  its validation and save workflow are tested in both the harness and browser.
  Compact readings are merged with the generated UI catalog, with regression
  checks for missing/unknown/duplicate addresses, stale errors and retained
  metadata after a single-register read.
  Partial fault scans check the abort reason, received count,
  skipped versus failed values, descriptive tooltips, response timeout margin,
  older firmware fallback, and warning removal after a successful refresh.
  Configuration opens cached-only with no Modbus read. Tests cover cached and
  missing rows, mandatory fresh edit reads, the 0x0017 pressure editor,
  per-category refresh, cancellation and three-failure stopping with retained
  values. Both cached values and failed pre-edit reads keep OK disabled.
- `bundle.mjs` builds the real plugin with the repository's production Webpack,
  Babel and MUI configuration. Temporary bundle output is removed afterward.
- `browser.mjs` renders `preview.tsx` with fixture-backed requests in isolated
  headless Chrome. It checks interactions and table/viewport width at 1280, 390
  and 320 pixels and writes screenshots plus `result.json` into the temporary
  directory printed on completion. The localhost test server and browser close
  after the run. No device connection is made.

The browser check uses the Codex bundled Playwright package and installed Chrome.
Set `BFS_PLAYWRIGHT_ROOT` to a different directory containing the `playwright`
package if needed. Other dependencies resolve from `core/esp32m/web-ui/node_modules`.

## Backend fixture

`fixture.json` contains actual JSON emitted by the production C++ BFS driver with
fake serial responses. It includes compact readings for all 80 parameters,
telemetry and historical faults. Descriptors and full group labels come from the
generated UI catalog, not the wire response. Its readings are test data, not a live device
snapshot. Regenerate it after changing the backend response format or registry:

```powershell
node core/esp32m/tests/bfs-driver/run.mjs core/esp32m/tests/bfs-ui/fixture.json
```

For the complete existing TypeScript workspace check:

```powershell
node core/esp32m/web-ui/node_modules/typescript/bin/tsc --project core/esp32m/web-ui/tsconfig.json --noEmit --incremental false
```

As of 2026-10-09 this reports six unrelated MUI prop errors in RS485 debug,
LF Bros and the Modbus scanner, with no BFS errors.
