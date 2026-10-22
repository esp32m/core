# Logs UI checks

Run `node tests/logs-ui/run.mjs` from the shared component root. The harness
transpiles production TS/TSX using the existing web-ui dependencies and exercises
the real hooks/components with deterministic React and backend doubles.

Coverage includes old firmware discovery, reconnect and obsolete responses,
bounded metadata/content pagination, literal text rendering, native download URLs,
closing during a request, visible read errors without automatic retry, explicit
settings save/cancel, quota units, destructive-retention warning, invalid settings,
and duplicate save suppression. These checks do not replace a real browser's
download/layout checks or device filesystem tests.

The production shared `useSubscribedState` hook is exercised in the harness.
Descriptor-gated subscriptions supply card state and release their interest on
unmount/disconnect. Pushes update capture, counters, policy and availability
without private `state-get` reads or metadata/history polling. Reconnect waits
for a live snapshot; unsupported firmware creates no interest. The card has no
Refresh action; the bounded browser retains its manual file controls.

Quota checks cover the default/saved 50% mode, percent/KiB/MiB conversions,
whole-percent validation, save/cancel/reopen, legacy fixed-byte settings and
capability gating, own-log-excluding capacity, and reserve pressure. A disabled
percentage policy remains saveable with little space; capture cannot be enabled
below the minimum effective quota.

## Real System UI fixture

From the shared component root:

```powershell
node tests/logs-ui/serve.mjs 9191
```

Open `http://127.0.0.1:9191/`. This renders the actual shared System page with
MUI/theme, Redux, backend hooks, and local in-memory test responses. It connects
to no device. The server provides native attachment downloads from the same
deterministic fixture data. Stop it with Ctrl+C. If the Windows network sandbox
isolates browser and server, run both in the same authorized host environment.

Optional cases: `?mode=empty`, `?mode=unsupported`, `?mode=unavailable`,
`?mode=stale`, `?mode=loss`, `?mode=legacy`, `?mode=lowspace`.
Translations: `?lang=uk`, `?lang=de`, `?lang=it`.

While the server is running:

```powershell
node tests/logs-ui/browser.mjs http://127.0.0.1:9191/
```

The browser check uses installed Chrome and the existing Codex Playwright runtime
(override its module directory with `LOGS_PLAYWRIGHT_ROOT`). It checks actual
dialogs and viewport overflow at 1280, 390, and 320 px, verifies a native download
against the complete expected file bytes, and checks unavailable/empty/loss
states and browser errors. Screenshots, the downloaded file, and `result.json`
are saved in the printed temporary directory.

## Representative production bundle

```powershell
node tests/logs-ui/production.mjs
```

Builds the real `../../projects/water-well/web-ui` entry with the production
Webpack/Babel/MUI builder and installed shared dependencies. It writes only a new
temporary output directory and prints its location; it does not run firmware
build scripts, alter the app's build output, or flash a device.
