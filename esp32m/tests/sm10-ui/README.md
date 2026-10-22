# SM10 UI checks

Run from the workspace root:

```sh
node core/esp32m/tests/sm10-ui/run.mjs
node core/esp32m/tests/sm10-ui/bundle.mjs
```

The interaction harness compiles the actual `Content.tsx` with the installed
Babel toolchain, then executes it with deterministic React hook and backend
doubles. It verifies optional/stale telemetry, on-demand history reads, ID and
total input limits, explicit configuration acknowledgment, the separate total
confirmation, pending actions, and visible errors without automatic write
retries. It does not test browser layout or MUI's own behavior.

The bundle smoke imports the real SM10 plugin and its dependencies with the
project's production `WebpackConfigBuilder`, Babel, MUI handling and minifier.
It omits the application HTML shell and writes only to a temporary directory.
Neither check opens a serial port or sends anything to a device.
