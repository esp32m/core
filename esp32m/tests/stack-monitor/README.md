# Stack monitor host tests

Run `node tests/stack-monitor/run.mjs` from the component directory. The tests
compile the unchanged production monitor with fake FreeRTOS allocation/snapshot
and logging functions, plus a disabled configuration with no platform symbols.

Covered behavior includes the threshold boundary, poll interval, accumulated
material new lows and zero reserve, task deletion and recycled identities,
bounded warning output with deferred reports, capacity races and retry,
allocation failure with preserved warning history, failure log rate limiting,
and bounded retained memory during repeated task creation/deletion. Snapshot
task-name and stack pointers deliberately point to inaccessible addresses;
logging and allocation assertions ensure they happen outside snapshot capture.

These tests validate policy and snapshot consumption. They do not measure
on-device stack demand, cross-core timing, or FreeRTOS snapshot latency.
