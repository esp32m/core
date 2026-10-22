# WebSocket subscription coordinator tests

Run `node tests/ws-subscriptions/run.mjs` from the esp32m component root.
The runner compiles the production `src/ui/subscriptions.cpp` with ArduinoJson 7
and a native C++ compiler; only capture, transport delivery and session liveness
are injected. Set `ARDUINOJSON_INCLUDE` / `CXX` if the default local paths differ.

The eight scenario groups cover shared stream allocation and immutable payloads,
unchanged-state silence, coalescing, canonical query identity, duplicate interest
delivery, atomic subscribe/resync commits, concurrent invalidation, structural
patches, explicit null/removal and tuple semantics, resource rejection/cleanup,
connection generations, timer rollover, and strict bool/number/string/null
category changes in scalar patches. Clock metadata remains outside revisioned
state: advancing time alone is silent, while a new acquisition with equal values
publishes its new timestamp. Sample times and clock references retain 64 bits.

These are host tests. They do not establish device timing, physical sampling,
heap headroom under the actual firmware workload, or HTTPD socket behavior.

## Resource model

`StateSubscriptions` retains one allocator-accounted JSON baseline per distinct
target/view/query. Every active stream shares that baseline across its interests.
There are at most 24 streams, 32 interests per session and 128 interests overall.
The baseline pool limit is 48 KiB, each snapshot is limited to 16 KiB, and the
combined baseline/working JSON allocation limit is 72 KiB. Allocation accounting
includes actual ArduinoJson allocation requests, slack and coordinator headers;
stream objects/strings and libc heap metadata are additional bounded overhead.

Only one source is captured at a time on the UI task. Its existing `getState()`
implementation temporarily owns its original JSON document while the capture
adapter copies into the bounded document. That original source allocation is
outside the coordinator allocator. The adapter also rejects source snapshots
larger than 16 KiB on the wire. The existing API cannot constrain the original
serializer's allocations before it returns.

HTTPD enforces a separate 32 KiB / 32-work output admission budget. Charges
include each work object, string capacity and a conservative 64-byte allocation
overhead, even when several destinations share the same immutable body.
Admission failure or asynchronous send failure disconnects the affected
generation. A full snapshot is obtained on reconnect; no client snapshot or
patch history is retained. Generation checks cover queued input, delayed
responses, sends and physical socket shutdowns.

The 16 KiB incoming frame limit bounds wire input, while existing JSON parse and
queued-request allocations remain separate from the subscription/output budgets.
These limits are not a promise that arbitrary source serializers plus every
other firmware task can fit a particular device heap.
