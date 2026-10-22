# CSF09 UI checks

Run from the repository root:

```powershell
node core/esp32m/tests/csf09-ui/run.mjs
node core/esp32m/tests/csf09-ui/bundle.mjs
```

The test runs the production TSX component and plugin with the existing UI Babel
toolchain and mocked state/hooks/transport. It checks startup/stale/invalid ages,
one-decimal units, missing measurements, address filtering and named modules.
Settings checks cover on-demand reads, ID validation, fixed read-only 9600/8N1,
disabled/pending/unverified locking, duplicate-submit prevention, address-only
30-second writes, independent read-back after success and failure, and explicit
read-only recovery without repeating an uncertain write. Settings remain reachable
without a valid measurement. The shared disable control is stubbed here; its
backend and interaction coverage lives in tests/sensor-polling.

Commissioning tests distinguish local `connectAddress` (15-second command,
read-only probe) from physical `address` changes. They exercise ID 1 to 9, an
absent ID 9 followed by connecting the next factory sensor at 1, and assigning
that sensor ID 10; mixed payloads and overlapping actions are rejected by the
harness. Pending/disabled/unknown states, failed probes, lost replies and failed
read-back are covered without automatic retries. An integration group executes
the production plugin registry and device container to confirm two named CSF09
plugins both register and render cards with separate backend module names.

The production Webpack smoke check imports the plugin through the public UI
barrel and verifies a complete bundle without errors or warnings, using the
installed dependencies. Temporary build output is removed afterward.

These checks do not validate sensor hardware or live Modbus communication.
