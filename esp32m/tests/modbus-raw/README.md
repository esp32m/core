# Raw serial ownership tests

Run `node core/esp32m/tests/modbus-raw/run.mjs` from the workspace root.
The runner compiles the production Modbus source against a mocked UART and
controller. It checks fragmented/noisy replies, callback completion, bounded
timeouts, overflow, single transmission, setup/cleanup/restoration failures,
restored baud/parity/running state, and refusing an unowned installed driver.
It also checks optional single-attempt requests: timeout/invalid-response
recovery resynchronizes the controller without repeating the write, while
ordinary requests retain the existing retry behavior.
Recovery deletes the controller even after the final failed attempt, so a
queued completion cannot be consumed by the next slave. Tests cover idempotent
start, failed deletion/recreation, and refusing reuse until recovery succeeds.
A retained callback writes after a simulated timeout/failed teardown to check
that the caller's buffer is protected. Successful register and packed-bit reads,
write payloads, failed-read isolation, null pointers and oversized data are covered.
Optional pre-request quiet intervals are checked under the UART mutex using a
competing thread, including retries and single-attempt writes. Requests without
an interval retain their existing timing.
The 100 Hz tick-phase regression models FreeRTOS delays at five positions in
a tick for 12, 1, 4 and 100 ms guards. It requires the elapsed interval to meet
the requested minimum while the mutex stays held, including sub-tick guards,
and checks that no-guard calls remain immediate. The old single `delay(12)`
fails this test because it rounds down to one tick and can wake almost at once.
These tests do not establish physical UART/RS485 timing or hardware behavior.
