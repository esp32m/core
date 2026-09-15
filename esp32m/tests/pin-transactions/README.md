# GPIO transaction and HBridge regressions

Run `node core/esp32m/tests/pin-transactions/run.mjs` from the repository root.
Requires a C++17 host compiler with thread support; set `CXX` if needed.

The harness compiles the unchanged production `Tx` declaration and methods,
the HBridge hardware methods (`init`, `run`, `refresh`, `setPins`, `getPins`),
and `Mux74hc::selectChannel`. Only platform services, the device shell and
physical GPIO providers are fakes. The runner writes generated source to a
unique temporary folder and removes it after execution.

Coverage includes task ownership, exclusion of another transaction and raw
provider accesses, nesting restoration and explicit commit, multiple provider
finalization and input snapshots, failed commits, cancellation of partially
staged commands, absence of stale replay, and Off-before-direction startup.
These are host regressions, not ESP-IDF scheduler or live I2C validation.

Providers participating in `Tx` must acquire `Tx::mutex()` for all raw and pin
accesses, register their finalizer before staging changes, use provider-specific
read keys, and implement `abort()` to discard staging. Nested scopes share the
outer batch: explicit inner commit flushes it; any abort cancels it. Cross-chip
commits cannot be electrically atomic and software rollback cannot undo a
hardware write that has already completed.
