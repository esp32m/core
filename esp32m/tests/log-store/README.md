# Persistent log service host tests

Run `node tests/log-store/run.mjs` from the shared component root. The runner
extracts the unchanged production `LogMessage` allocation/copy/formatter and
complete `Store` implementation, compiles them against host platform/transport
stubs, and links the real portable storage engine and ArduinoJson. Set `CXX` or
`ARDUINOJSON_INCLUDE` when the default local dependencies are unavailable.

The suite verifies saved policy restoration before recovery, the durable ID
floor, transient capacity-provider failure without history loss, repeated stable
clock synchronization and clock-jump invalidation, bounded nonblocking queue
capture, original task/logger/timestamp data,
UTF-8 truncation at every byte boundary, overflow/scheduler drop accounting,
settings validation, list/read cursor limits and generations, idle-priority
worker/notification waits, graceful accepted/in-flight queue drains before
disable/shutdown sealing, format/OTA lifecycle, export byte equality, combined export boundaries,
snapshot pins, busy browsing, cancellation, and cleanup after worker creation,
async request, network, and timeout failures. The storage worker's independent
export deadline is checked before the export task progresses, including socket
shutdown and pin cleanup.

Lazy-resource scenarios verify that disabled boot/catalog access allocates no
capture queue or worker; enable, accepted-record drain, disable and re-enable
release/recreate both resources. Disabled retained-file age, quota, catalog and
format maintenance runs through `EventPeriodic`. Downloads while disabled create
only the export/watchdog tasks, preserve the independent two-minute deadline,
and retire the watchdog after completion/cancellation or async-begin failure.
Queue allocation/undersized-buffer failures and worker allocation failures
release partial resources and recover on periodic retry. A producer holding an
unfinished slot past the bounded disable drain keeps its queue alive, publishes
later, and is persisted/sealed before queue deletion. The ring stub marks freed
queues and rejects subsequent use. Producers never notify a retiring task handle.

Quota coverage includes the new disabled-capture/50 percent default; all integer
percentages from 1 through 100; atomic rejection of negative, fractional,
out-of-range and malformed percentages; legacy absolute-byte policy restoration;
switches between byte and percentage modes; and partial enabled/retention updates
that preserve mode. Configuration, state and info report the current resolved
budget and free-plus-owned-log basis. Recovery must exclude existing owned
history from external usage before enforcing a saved percentage policy. Small
percentage budgets remain valid saved policies while capture pauses without
creating files; their serialized config restores correctly and capacity recovery
resumes capture. Temporary capacity-provider loss preserves percentage history.
The percentage basis is asserted immediately after the boot marker and after
each record in a six-message burst that remains unflushed, so buffered owned
bytes cannot inflate the resolved budget before the next capacity sample.

State subscription coverage records normal `EventStateChanged` hints from the
extracted service. Its test listener immediately re-enters production
`getState()`, proving publication releases the Store mutex and snapshots use
cached filesystem/clock data. The normal UI consumer defers this capture.
Healthy idle periods, repeated reads and persistent unavailable, low-budget,
queue-allocation and boot-marker errors must not flood hints. A resolved marker
failure publishes resumed capture. Producers never publish inline; worker write bursts and
drop counters coalesce to one telemetry hint per second while retaining the
final update. Capacity, configuration, export, format and clock transitions
invalidate subscribed state, with an `EventPeriodic` fallback if worker creation
fails. The runner's process timeout bounds a failed lock-order regression.

Stubs exercise production decisions but do not emulate FreeRTOS scheduling,
ESP-IDF HTTP server/socket behavior, NVS flash durability, or physical filesystem
latency. Run the separate portable storage and HTTP foundation suites plus a real
firmware build; device performance and power-loss checks remain separate.
