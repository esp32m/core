# WebSocket state subscriptions UI tests

Run `node tests/ws-subscriptions-ui/run.mjs` from the core project root.

The suite executes the production TypeScript client, subscription manager, Redux
reducer and hooks with deterministic transport/timers. It covers shared consumer
lifetimes, canonical query identities, device metadata views, initial snapshot
ordering, patch application, revision gaps/resync, suspension, reconnect, obsolete
acknowledgments, command/read races, conditions, query changes and one-shot reads.
It also verifies the bounded connection heartbeat and recovery of a silent link.
Terminal errors stay quiet until explicit retry, transient failures back off,
and omitted/null queries share one interest and cache.
Exact timestamp tests execute the production server clock and age hook:
timestamp 0, unknown/future stamps, one shared timer and cleanup, equal-valued
new acquisitions, local ticks without WS traffic, old initial samples,
disconnect/reboot, and one-shot view connection provenance.
