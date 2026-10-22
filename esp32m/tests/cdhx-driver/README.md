# CDHX driver host regression tests

Run `node core/esp32m/tests/cdhx-driver/run.mjs` from the workspace root.
The runner compiles the production header and source bodies unchanged with
fake UART/GPIO/tasks/framework services and an independent clock. It needs
MinGW/GCC and ArduinoJson 7; `CXX` and `ARDUINOJSON_INCLUDE` override defaults.

Twenty-seven groups exercise forwarding, exact echo consumption, one-shot commands,
source/emitted/feedback separation, persistent setting priority,
priming exclusion, explicit power bounds, invalid/partial packets, uncertain
TX, timeouts, stale controls, initialization-safe sensor events, UART/GPIO
ownership, and optional bypass qualification/polarity/settling/manual/fault paths.
The timing group exercises pinned UART initialization, a1ms wake timer with a
100Hz host RTOS configuration, delayed/late exact echoes, first-fault evidence,
faulted RX discard, startup failure cleanup and synchronized timer shutdown.
The buffered-receive group recreates the captured24.3ms owner pause with an
exact five-byte echo tail and a partial CRC-valid frame. It also verifies that
wrong/missing bytes and expired transaction correlation still stop forwarding.
It also injects a25ms pause inside the owner step after display RX was drained,
then queues the captured17/seven-byte echo boundary. Correct recovery must
consume the exact tail, retain mismatch/UART-error evidence, and still reject a
missing byte after a subsequent empty read. Mid-step pauses after copying a
whole packet must not submit a stale queued Start or publish an expired reply.
A destination UART preflight pause must also preserve the original response
deadline until submission, without publishing or falsely confirming that reply.

Production configuration groups cover indefinite persisted mode/value settings,
both remembered setpoints, atomic service/operating edits, strict types/envelopes
and separate stock/desired/submitted state. Physical display edits, release,
startup/cooldown, faults, reset and bypass retain authority. A70-second test keeps
servicing the owner every1ms to prove the former60-second lease has gone without
relaxing the existing stall policy. Same owned-mode running edits, stopped mode
changes, measured thermostat zero and ambient fallback/resume are explicit.
Production saves accept edited values without UI reload or stale identity guards;
research actions retain their independent identity guards and finite leases.

Persistent service groups compile the actual `getConfig()`/`setConfig()` bodies.
They check one-time valid normal76 adoption, deferred configuration publication,
offline internal restore, restore before first interception, and strict atomic
external ranges/steps/types. They distinguish physical, effective and submitted
tuples, require stopped fresh traffic and no pending command/research owner, and
preserve commands/priming while replacing only service bytes. Availability and
admission remain stable through transient request/reply/echo phases. Tests
also retain authority across release and physical service edits,
partial TX faults, reset and requested bypass/requalification. Raw-power checks
use the effective service profile, and final UART preparation uses the current
authority. These tests establish software encoding and persistence behavior;
they do not qualify operating combinations on a real heater.

Six asynchronous configuration groups execute the actual captured worker task.
The caller returns immediately; the worker owns copied request data and responds
once only after a new matching request/reply and both local echoes. No-op saves
also require a new exchange. Tests cover bounded worker capacity, stale visible
metadata, request destruction, validation/allocation failures, superseding edits,
partial-TX faults and a2-second wait timeout. Saved settings remain after uncertain
results, and no retries, events or response serialization run in the UART owner.
Service-only saves can confirm their service substitution while saved thermostat
settings remain suspended for missing stock ambient. Combined operating/service
edits still need valid operating admission and cannot commit either half alone.
Restored raw power outside caller bounds, a disabled power range, or a changed
qualified service/opaque/header profile suspends power substitution while keeping
the desired value. Tests confirm automatic resume when qualification returns.
Service-only profile edits still receive successful deferred confirmation for
their applied service fields while the saved power output remains suspended.
Fault/reset or bypass/requalification entirely between worker polls cannot become
a false success even if a fresh matching exchange has completed before polling
resumes. The response retains interruption history and saved settings.

The thermostat group compiles the actual typed snapshot and transient request
seam. It checks qualified low-output feedback, no configuration writes, lease
expiry, local display takeover, rejection of unqualified ambient substitution,
expired queued Start cancellation and cooldown restart inhibition.

These checks validate software behavior. They do not measure GPIO waveforms,
display timing acceptance, production reset/brownout/unpowered bypass, or
operating-heater behavior.
