# State observation regression test

Run `node core/esp32m/tests/state-observation/run.mjs` with Node.js and MinGW g++.
The harness compiles the actual AppObject declaration, EventStateChanged, and
request-dispatch implementation with real ArduinoJson and fake event/platform
services. It verifies mutation, failed-operation and custom-command invalidation,
the absence of read/capture feedback loops, and invalidation after complete
handlers without inline serialization. No hardware or network access is used.

Related complete-sample and disable transition checks live in
`tests/sensor-polling/run.mjs`; emitter scheduling remains covered by
`tests/state-emitter/run.mjs`.
