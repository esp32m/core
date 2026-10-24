# CrashGuard recovery notice host tests

Run from the esp32m source directory:

```sh
node tests/crashguard-ui/run.mjs
```

The test transpiles the production TypeScript/TSX and exercises the recovery
notice, its connection capability hook, default plugin registration, and the
shared navbar layout with deterministic React hooks and transport doubles.
It covers old/disabled/unsupported firmware, snapshot-plus-change subscriptions,
stale reconnect caches and delayed capability responses, fallback/replacement
wording, same-version SHA identities, inline details without the Debug plugin,
and persistence above the route outlet on navigation.

These host checks do not induce device crashes or operate hardware.

For a device-free visual preview using the production presentation component
and real React/MUI, run `node tests/crashguard-ui/preview.mjs`. It writes the
ignored `../ai/debug/crashguard/preview.html` with wide and narrow fallback,
replacement, and normal-state examples.
