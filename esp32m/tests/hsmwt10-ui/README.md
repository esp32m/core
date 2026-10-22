# HSM-WT10 UI checks

Run from the workspace root:

```powershell
node core/esp32m/tests/hsmwt10-ui/run.mjs
node core/esp32m/tests/hsmwt10-ui/bundle.mjs
node core/esp32m/tests/hsmwt10-ui/browser.mjs
```

`run.mjs` compiles the production `Content.tsx`, `types.ts` and `values.ts`
with the workspace's Babel packages. It checks component control flow and
visible values using deterministic React hooks, effect cleanup, a local clock
and backend doubles. It makes no network requests.

The interaction groups cover:

- Leak/normal/unknown telemetry, raw values, missing and invalid snapshots,
  rejection of cached readings after mount/module changes, locally aging stale
  state, new-snapshot recovery, and timer cleanup.
- Configuration reads only when opened/refreshed, custom module names and
  titles, and stable module routing that keeps the card and partial-failure
  dialog visible after address migration, including revisiting the page.
- Strict integer/rate validation, missing or failed configuration reads, and
  both configuration and telemetry pending states preventing writes.
- Changed fields only, one outstanding operation, duplicate action suppression,
  and disabling edits/close actions while a request is active.
- Independent configuration read-back and state refresh after every write;
  only verified success closes the editor.
- Partial updates or lost write replies retaining a visible error and reloading
  actual settings without automatic retries.
- Mismatched read-back, lost read-back, read-only recovery, and failed refreshes
  invalidating earlier editable baselines.

`bundle.mjs` separately checks the production Webpack/Babel/MUI build using the
public `HsmWt10` plugin export, including its default and custom name/title
registration. Temporary bundle output is removed afterwards.

`browser.mjs` separately renders the real React/MUI component in headless Chrome
with an in-memory fixture backend. It checks interactions and viewport layout
at 1280, 390 and 320 pixels. The server binds to loopback, external requests are
blocked, and screenshots plus `result.json` remain in the temporary directory
printed by the command. The test server and browser close after the run.

The browser check uses the Codex bundled Playwright package and installed
Chrome. Set `HSMWT10_PLAYWRIGHT_ROOT` to another directory containing the
`playwright` package if needed. Other dependencies resolve from
`core/esp32m/web-ui/node_modules`.

All three checks are fully local and use test fixtures or backend doubles.
They do not access or reconfigure live hardware.
