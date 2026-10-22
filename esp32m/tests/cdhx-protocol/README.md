# CDHX protocol host checks

Run `node core/esp32m/tests/cdhx-protocol/run.mjs` from the repository root.
The runner uses `CXX` or a known MinGW C++ compiler and builds in a temporary
directory. No ESP-IDF or hardware is required.

The eleven scenario groups check published/captured CRC vectors and high-first
wire order, mode-aware request decoding, signed response fields, one-copy command
patches that preserve every tuning byte, thermostat mode/strict limit checks
(equal bounds are refused) and rejected
patch immutability, parser recovery after noise/bad CRC/false headers, and partial
frame expiry/recovery. Atomic mode tests change only ambient/value/mode and CRC,
reject unsupported sources/commands/priming, and retain measured thermostat zero.
Service-template comparison ignores only command, ambient, value and mode;
all tuning/opaque fields and packet validity remain part of qualification.
Service tests cover every qualified range endpoint, pump ordering, exact fan
steps, voltage/Hall/glow enumerations and rejected-mutation immutability. Service
substitution preserves command, prime, mode, setpoint, ambient, temperature limits
and opaque bytes for both normal headers, and recomputes the high-first CRC.
Persistent operating patches preserve physical commands and priming while
changing only ambient/value/mode and CRC. The strict laboratory mode helper
continues to reject commands and priming. Both reject unsupported thermostat
ambient/limits and invalid frames without modifying caller-owned bytes.
These software checks do not validate electrical behavior,
heater control acceptance, physical telemetry calibration, or fail-safe operation.

Source evidence: `C:/work/ai/cdh/H45214A0/DISPLAY_PROTOCOL.md` and
`C:/work/ai/cdh/H45214A0/analysis/test_display_protocol.py`.
