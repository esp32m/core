# Sensor polling regression test

Run with Node.js and Windows MinGW g++ (C++17):

```powershell
node core/esp32m/tests/sensor-polling/run.mjs
node core/esp32m/tests/sensor-polling/ui.mjs
```

`CXX` may select another compiler executable. The runner also recognizes the
workspace machine's installed MinGW compilers. A compiler with a 32-bit
`unsigned long` is required to match ESP32 retry arithmetic.

The runner extracts the **complete, unchanged production Device declaration**
and the **unchanged sensor scheduler implementation** from `device.hpp` and
`device.cpp`, then compiles them with a fake monotonic clock, event delivery and
FreeRTOS waits. ArduinoJson is real; unrelated platform services are stubs. Generated
source and the executable live in a temporary directory and are removed after
the run. No firmware, network connection, or physical device is involved.

Checks cover startup, deadline equality, positive sub-tick waits, mixed deadlines
on either side of the 49.7-day boundary, a missed pre-wrap deadline, several
missed rollover periods, completion-based intervals, initialization retry timing
(including across wrap), empty schedules, watchdog wait bounds, and invalid or
very long intervals. These tests validate scheduling behavior, not ESP-IDF task
integration or physical sensor communication.

Device-disable checks cover strict configuration validation, separate toggle
requests, persistence/restore, disabled initialization/polling, command rejection,
cached value clearing, re-enable, waiting for an active poll, and skipping a busy
device without blocking another. UI tests run the production shared control and
MAQI card, including lost replies recovered by read-back without duplicate writes.
The C++ runner uses the workspace greenhouse project's ArduinoJson dependency;
`ARDUINOJSON_INCLUDE` can point to another ArduinoJson 7 source directory.

Subscription observation checks cover exactly one invalidation per complete
sample, equal successful samples, failed polling/initialization, disable/re-enable,
and state reads waiting for complete multi-component samples. Busy observation
also remains nonblocking for other devices, including drivers without CanDisable.
