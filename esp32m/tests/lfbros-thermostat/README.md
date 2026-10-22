# LF Bros thermostat seam host checks

Run `node core/esp32m/tests/lfbros-thermostat/run.mjs` from the workspace root.

The runner extracts the actual driver snapshot, transient control, serialized
command and observation bodies from `src/dev/lf-bros.cpp`; fake monotonic time,
queue and CAN submission replace the platform. It does not duplicate the room
policy or LF native state machine. The production declaration and direct
Actuator methods are compiled too; calls use the device through Actuator&.

Checks cover qualified native temperature and minimum-output feedback, native
standby capability, no supported ambient injection, transient power without
configuration writes, lease expiry, raw manual takeover, cancellation of queued
Start on release/expiry/new fault, observed command completion, unconfirmed CAN
submission, partial/stale feedback and unchanged legacy Start sequencing.
Attachment checks cover legacy HA suppression/restoration, rejected second
owners, mismatched detach and absence of CAN commands during attachment.

Production ESP-IDF compilation is checked separately with
tests/thermostat/compile.py. Supervised heater acceptance is still needed; this
harness does not validate CAN electrical timing or combustion.
