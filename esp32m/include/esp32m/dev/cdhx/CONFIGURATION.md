# CDHX production Configuration API

The Configuration dialog edits persistent operating and 1688 service settings.
The ESP owns these values indefinitely, including after reboot. Physical-display
edits do not replace them. The reusable driver works without the removable
project-local research extension.

Requests use the existing `/ws` envelope and explicit target `cdhx` (or the
configured driver name). Opening the dialog sends no control action.

## Save operating settings

Submit the selected mode and its integer setpoint together:

```json
{
  "type": "request",
  "seq": 2,
  "target": "cdhx",
  "name": "config-set",
  "data": {
    "operating": {
      "mode": "temperature",
      "temperatureSetpointC": 18
    }
  }
}
```

For `mode:"power"`, supply `fixedPowerSettingRaw` instead. Both setpoints may
be included; an omitted inactive-mode value retains its saved value. The selected
mode's setpoint is required. Unknown, null or incorrectly typed fields reject
the entire update. Broadcast writes are rejected.

There is no duration, lease or expiration. No preceding read, matching revision,
boot identifier or acceptance deadline is required. Legacy production guard
metadata is accepted and ignored for compatibility. Internal generations track
deferred completion; they are not a user read-before-write prerequisite.

Without restored settings, the driver adopts the first valid normal `76` request's
mode and selected setpoint once. It does not invent an inactive-mode default.
The full stored configuration has this shape (only known values are emitted):

```json
{
  "operating": {
    "mode": "temperature",
    "temperatureSetpointC": 18,
    "fixedPowerSettingRaw": 13
  },
  "service": {
    "minimumPumpHz": 1.4,
    "maximumPumpHz": 5.4,
    "minimumFanRpm": 1500,
    "maximumFanRpm": 4300,
    "configuredVoltageV": 12,
    "hallCount": 1,
    "glowPowerLevel": 5
  }
}
```

The numbers above illustrate the schema, not defaults. Internal esp32m config
restoration validates stored values without requiring a live heater or standby.
Initial bridge arming waits for App initialization/config restoration.

## Deferred response

External `config-set` owns a deferred esp32m Response. A low-priority task
validates and saves the desired tuple, publishes configuration changes outside
the UART owner mutex, then waits for a new forwarded request/reply pair carrying
those settings and both local TX echoes. Transaction phase does not affect edit
eligibility or make buttons flicker. The UI remains busy until the response.

The response returns the current configuration on forwarding completion.
A bridge fault, bypass, a superseding save or a two-second confirmation timeout
returns an error. If acceptance already happened, the saved values remain owned:
an error does not roll back the tuple or automatically retry a heater command.
Inspect state/configuration to resolve an uncertain forwarding outcome.

This receipt confirms local transport only. It is not a heater mode ACK, LCD
synchronization or confirmation of an immediate nonvolatile commit. Persistence
uses esp32m's standard deferred configuration saving.

Native `setTemperature()`, `setFixedPower()` and `setConfiguration()` return
immediate acceptance for application on the next normal request. The compatible
`state-set.configuration` action has the same acceptance semantics; the UI uses
deferred `config-set`. Native legacy guard members are ignored.

## Edit eligibility and ongoing authority

- Edits require an active bridge and fresh normal traffic from both sides, with
  no unresolved Start/Stop or command/priming operation.
- Changing the saved ESP mode requires a fully stopped heater:
  state0/status0/fan0/pump0. Same-mode edits also allow healthy steady running
  state5/status1. These are edit checks, not expiration conditions.
- Temperature edits need fresh physical-display thermostat ambient in 0..127°C,
  including measured zero, and a setpoint within the captured ordered,
  nonnegative temperature limits. Power-mode D3=0 is a placeholder.
- Fixed power needs a caller-qualified raw range. Optional exact service-template
  binding detects altered tuning/opaque fields. There is no universal power scale.
  If restoration or a later profile change leaves a saved power value outside
  that qualification, retain it and forward display control until qualified
  again. Service-only saves can still complete during operating suspension.
- If saved temperature control loses usable physical-display thermostat ambient,
  retain the desired tuple and temporarily forward display control. Resume the
  saved temperature setting automatically when compatible ambient returns.
- Fault/reset and hardware bypass retain saved settings. While bypass directly
  connects the display and heater, ESP substitution cannot act; it resumes with
  bridge operation. Start/Stop remain single pulses and are not replayed.

Operating substitution changes D3/D4/D13 and CRC while preserving physical
command/priming and opaque bytes. Service substitution also applies independently.
Incoming heater replies remain untouched. The physical LCD retains its own mode
and values; forwarding does not update its nonvolatile settings.

## Persistent 1688 service settings

Send a full seven-field service tuple:

```json
{
  "type": "request",
  "seq": 3,
  "target": "cdhx",
  "name": "config-set",
  "data": {
    "service": {
      "minimumPumpHz": 1.4,
      "maximumPumpHz": 5.4,
      "minimumFanRpm": 1500,
      "maximumFanRpm": 4300,
      "configuredVoltageV": 12,
      "hallCount": 1,
      "glowPowerLevel": 5
    }
  }
}
```

Operating and service objects may be submitted together; validation and commit
are atomic. Service edits require fully stopped fresh traffic. Unknown, missing,
null or incorrectly typed values reject the whole update. There is no reload
prerequisite. Without saved service settings, the first valid normal `76`
request with supported mode and no command/priming is adopted once.

Every normal forwarded `76`/`78` request substitutes D5..12 and D16 and
recomputes CRC. Display service edits do not replace the saved tuple.

| Field | Accepted values | Encoding |
| --- | --- | --- |
| `minimumPumpHz` | 0.8–2.5, step 0.1 | D5 ×0.1 Hz |
| `maximumPumpHz` | 2.2–8.0, step 0.1; at least minimum | D6 ×0.1 Hz |
| `minimumFanRpm` | 1350–2600, step 10 | D7–8, big-endian RPM |
| `maximumFanRpm` | 3500–5000, step 10 | D9–10, big-endian RPM |
| `configuredVoltageV` | 12 or 24 | D11 ×0.1 V |
| `hallCount` | 1 or 2 | D12; H452 ignores it and forces 1 |
| `glowPowerLevel` | Integer 1–6 | D16 setting level, not watts |

The unidentified final `oF` page stays unmapped. These are observed display
editing limits, not combustion qualification throughout all combinations.
Research source:
`C:/work/ai/cdh/H45214A0/captures/20261009-service-menu-ranges/AI_CONTEXT.md`.

## State and UI

`configuration.authority` identifies saved operating authority. `mode`,
`value`, `temperatureSetpointC` and `fixedPowerSettingRaw` describe desired
values; `active` describes current substitution. `phase`/`reason` distinguish
pending, attempted, local-echo, paired-response, suspended and uncertain states.
`canApply`, `modeChangeAvailable`, `ambientAvailable` and `powerAvailable`
describe edit eligibility. No lease/countdown fields are used.

`service` is the ESP tuple, `stockService` the latest display request, and
`submittedService` the last attempted heater request. `serviceAuthority`,
`serviceWritable`, `serviceCanApply` and `serviceReason` describe service
ownership and editing. The UI keeps editable drafts during polling and shows
differing display values below their fields, with stale snapshot age. Duplicate
effective/attempted service summary tables are omitted for writable firmware.

Stock values are observations of traffic, not a read of the display's
nonvolatile storage. Submitted values are attempted output, not heater ACKs.

## Temporary research and evidence boundary

Temporary research actions retain their finite leases and stale-action guards.
They do not change the production persistence contract. They may temporarily own
operating substitution only under the research engine's eligibility rules;
release/expiry returns to the persistent ESP baseline. Service authority still
applies. Research must not claim exact stock restoration when the ESP baseline
differs from physical stock.

Stopped H452 mode replacement has evidence in
`C:/work/ai/cdh/shared/mode-switch-research/2026-10-09-mode-results.md`.
Running mode changes, CDHX1 injected transitions, persistence and LCD
synchronization need separate physical qualification. Host tests/builds establish
software behavior and do not extend that hardware evidence.
