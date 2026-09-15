# Sensor polling regression test

Run with Node.js and Windows MinGW g++ (C++17):

```powershell
node core/esp32m/tests/sensor-polling/run.mjs
```

`CXX` may select another compiler executable. The runner also recognizes the
workspace machine's installed MinGW compilers. A compiler with a 32-bit
`unsigned long` is required to match ESP32 retry arithmetic.

The runner extracts the **complete, unchanged production Device declaration**
and the **unchanged sensor scheduler implementation** from `device.hpp` and
`device.cpp`, then compiles them with a fake monotonic clock, event delivery and
FreeRTOS waits. JSON and unrelated platform services are inert stubs. Generated
source and the executable live in a temporary directory and are removed after
the run. No firmware, network connection, or physical device is involved.

Checks cover startup, deadline equality, positive sub-tick waits, mixed deadlines
on either side of the 49.7-day boundary, a missed pre-wrap deadline, several
missed rollover periods, completion-based intervals, initialization retry timing
(including across wrap), empty schedules, watchdog wait bounds, and invalid or
very long intervals. These tests validate scheduling behavior, not ESP-IDF task
integration or physical sensor communication.
