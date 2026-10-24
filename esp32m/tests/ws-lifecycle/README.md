# WebSocket lifecycle regression tests

Run `node core/esp32m/tests/ws-lifecycle/run.mjs` with Node.js and MinGW g++.
The harness compiles production Client and Transport declarations, request
dispatch/session handling, and HTTPD send/close work methods against real
ArduinoJson with deterministic fake sockets and HTTPD work queues. C++20 is
required for the production atomic shared pointer.

Checks cover FD reuse, inbound generation at enqueue and each batched dispatch,
old queued replies, deferred response identity, shared payload ownership, async
send failure, queued close generation, queue-admission failure fallback, output
work/byte limits, released charges, and incoming request identity/count limits.
Failure coverage injects a failed checked payload and a failed nothrow output
work allocation, verifies charge rollback and closes only the matching connection
generation. Closing a client under pressure retains healthy broadcast delivery
before and after UI cleanup; queued predecessor payloads remain undeliverable.
Startup coverage queues eight singleton-array requests with unique sequences
before dispatch. Overflow coverage fills all eleven document slots and checks
each rejected batch request receives its own correlated busy response, including
write requests that must never dispatch or replay after connection replacement.
Fixed `wsStateReads` / `wsDeviceReads` diagnostics count accepted WS requests
and exclude direct internal capture events and rejected connection generations.
The handshake regression follows installed ESP-IDF 6.1: PRE authenticates,
IDF sends101, POST establishes the control generation, and IDF skips the normal
URI handler during upgrade. The first text frame then exercises the production
receive/queue/dispatch/send path. Custom endpoint admission remains in PRE.
No firmware, network connection, or physical device is involved.
