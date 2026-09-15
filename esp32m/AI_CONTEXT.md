# esp32m development context

## PCA / I2C / valve corrections (2026-09-13)

- GPIO Tx uses thread-local scope ownership and a global recursive mutex,
  provider-keyed snapshots, multiple finalizers and explicit commit/abort.
  Expander direct accesses share the lock. HBridge/mux propagate commit errors;
  PCF857x and AW9523 use the updated contract. HBridge initializes Off first.
- MasterBus serializes transfers, probing, reset and device lifecycle. Retries
  are bounded with nonzero RTOS backoff. INVALID_RESPONSE retry is opt-in for PCA.
- PCA verifies config/output/inversion and keeps only confirmed write caches.
  Explicit setRecoveryOutput(mask,value) policy is required to restore mismatched
  registers safely. Recovery writes safe latches before directions and fails the
  interrupted operation. Existing usePca95x5(address) calls remain valid;
  greenhouse selects Model::Pca9535C and safe mask 0x4300/value 0.
- Valve enforces 10s async/sync travel timeout, faults on control/sensor errors,
  retries unconfirmed Off every 250ms and requires a new command after fault.
  UI adds Stop and fault/stopPending diagnostics. Startup persistence remains.
- tests/{pin-transactions,i2c-recovery,pca95x5,valve-safety}/run.mjs all pass.
  Eight core objects plus greenhouse main compile with ESP32 flags/installed
  IDF 6.1 headers and retained IDF 6.0.1 compile DB/config. User will rebuild;
  no full build/flash/live actuation. Workspace tsc has only three existing
  LF Bros MUI prop errors (Content.tsx:212/291/312), no valve UI errors.
- Full report in parent workspace:
  projects/greenhouse/diagnostics/2026-09-13-pca95x5/implementation.md.

## Shared sensor scheduler rollover correction (2026-09-13)

- Garage-boiler 1.0.22 live evidence correlated stale PTMB and board NTC
  measurements with the 49.71-day uint32_t millis() rollover. Read-only PTMB
  config reads still succeeded; the bus itself was not continuously dead.
- Fixed Device polling in include/esp32m/device.hpp and src/dev/device.cpp:
  completion times, next deadlines, minimum aggregation, and waits now use
  uint64_t milliseconds from esp_timer_get_time(). Due checks include equality.
- Corrected the scheduler watchdog wait units (wdtTimeout() returns seconds),
  cap waits at 1 second and before the watchdog margin, and ensure positive
  sub-tick waits block at least one tick. Poll intervals <= 0 become 1 ms.
- Global millis(), sensor age JSON, and reinitialization's wrap-safe elapsed
  check remain as before. nextSensorsPollTime() is a protected virtual with a
  new uint64_t return type; no in-tree overrides were found. External overrides
  must return 64-bit monotonic milliseconds from the same clock.
- ESP32-S3 device.cpp and ptmb.cpp object compilation passed with existing IDF
  v6.1 flags. User will rebuild and reflash; no full build or live change made.
- Actual-source host regression passes via
  `node core/esp32m/tests/sensor-polling/run.mjs` from the parent workspace.
  It compiles the unchanged Device declaration and sensor scheduler with fake
  clock/event/RTOS dependencies. Five scenario groups cover old rollover
  boundaries, mixed/overdue deadlines, multiple wraps, completion timing,
  retry timing, equality, interval normalization, and bounded nonzero waits.
- Negative control: the same harness compiled pre-fix HEAD source and failed
  at runtime with `FAIL: deadline spans rollover`; the corrected source passes.
- Detailed original diagnosis and captures are under
  projects/garage-boiler/diagnostics/2026-09-13-rs485 in the parent workspace.

## LF Bros heater driver (2026-08-11)

### Scope

- Add a reusable LF Bros diesel-heater device driver in
  `include/esp32m/dev/lf-bros.hpp` and `src/dev/lf-bros.cpp`.
- Add its reusable web UI in `web-ui/esp32m/ui/src/dev/lf-bros` and export it
  from the core device UI index.
- Keep protocol experiments and capture tooling in the separate
  `projects/lf-bros-rev-eng` component. That project will select at compile
  time between `lfbros::useProbe()` and `dev::useLfbros()`.

### Confirmed protocol constraints to preserve

- ESP-side TWAI pins currently are TX GPIO6 and RX GPIO7 through a TJA1051
  transceiver pair; bus rate is 150 kbit/s with ESP32-S3 timing BRP 28,
  PROP_SEG 3, TSEG1 12, TSEG2 3, SJW 1 (80 MHz APB).
- The heater powers the ESP32 from its switched tool +5 V rail. TWAI must be
  initialized before `App::Init`; about 21.9 ms after reset is proven to work,
  while initialization near 286 ms missed the cold-start exchange and the
  heater removed +5 V after about 25 seconds.
- The driver must start receive/ACK-only, wait until a conservative 1-second
  cold-boot point, then send the read-only extended data request ID `0x205`,
  DLC 0. It must observe an actual application response before claiming tool
  control.
- Claim safe standby with extended ID `0x201`: `[04 01]` followed by
  `[03 00]`. Start is `[04 01]`, `[03 01]`; stop is `[03 00]`.
- In self-powered operation the driver must keep tool source selected, poll
  `0x205` at 1 Hz indefinitely, and never send release `[04 00]`, including
  after shutdown. Cutting battery power during heater cooldown is unsafe.
- A `0x205` response is extended ID `0xAA55`: DLC-5 header containing chunk
  count 23 and little-endian STM32 CRC32, then 180 payload bytes in 23 ordered
  chunks (22 x DLC 8 plus DLC 4). Payload starts with `EC3.80a\0`.
- Runtime offsets and state/error mappings are canonical in the experimental
  project's `CAN_PROTOCOL.md` and `host/lfbros/runtime.py` and must be ported,
  not re-inferred.
- Setpoint selectors are statically decoded but not yet live verified:
  selector 0 + float32 LE selects/clamps temperature 10..35 C; selector 1 +
  float32 LE selects/clamps power to heater-reported live limits. UI labels
  and documentation must preserve this confidence distinction until tested.
- Physical-display mailbox telemetry is not the tool command mailbox. While
  tool source is selected, display edits do not update the service runtime
  mailbox. The driver must distinguish locally commanded tool setpoints from
  observed physical-display values.

### esp32m architecture findings

- A driver derives from `Device`, implements descriptor/state/config/request
  methods, and can publish telemetry through `Sensor`, `BinarySensor`,
  `Switch`, or `Select` components.
- Home Assistant MQTT discovery can be customized by handling
  `ha::DescribeRequest`, `ha::StateRequest`, and `ha::CommandRequest`.
  A private custom component can still trigger the standard grouped MQTT
  state publisher while the driver supplies a richer discovery descriptor.
- Applications must instantiate `net::useMqtt()`,
  `net::mqtt::StatePublisher::instance()`, and
  `integrations::ha::useMqtt()` for HA; device drivers do not own the global
  networking integration.
- The production CAN worker should be initialized synchronously by
  `dev::useLfbros()` before `App::Init`. UI and MQTT callbacks enqueue typed
  commands to that worker; they must not transmit directly through TWAI.

### Resolved API and behavior decisions

- HA climate `current_temperature` uses runtime byte offset `0x8C` as ambient
  temperature for now. This is intentionally exposed so live operation can
  confirm or correct the interpretation. A future driver revision may accept
  an independent external room-temperature source because the heater's sensor
  is physically associated with the display.
- Control mode and both setpoints are persisted by esp32m. The ESP is the tool
  master and reapplies its values after acquiring a session, overriding the
  heater's own persisted setpoint. The ON command is deliberately not
  persisted: every ESP boot claims explicit OFF before applying a setpoint.
- `Lfbros::Options` requires explicit TWAI TX/RX GPIOs. The reusable helper has
  no GPIO6/GPIO7 defaults because pin availability and routing differ between
  ESP32 variants.
- The experiment/production selection is a source define in the experiment
  project's `main.cpp`, not Kconfig. This avoids sdkconfig regeneration and
  bootloader/SDK recompilation when switching modes.

### Implementation milestone

- Core device API is in `include/esp32m/dev/lf-bros.hpp`; implementation is in
  `src/dev/lf-bros.cpp`.
- `dev::useLfbros(options)` synchronously initializes TWAI before `App::Init`,
  then a dedicated worker owns all protocol RX/TX and serialized commands.
- The state API and UI expose session/CAN diagnostics, decoded heater state,
  fault information, chamber and ambient temperatures, voltage, fan, pump,
  glow-plug telemetry, and the physical-display mailbox separately from tool
  commands.
- Controls include safe start/stop, temperature/power mode selection, and
  temperature/power setpoints. Start is never issued automatically.
- Home Assistant publishes an MQTT climate entity, a power-setpoint number,
  the control-mode select, connectivity/fault entities, and telemetry sensors.
- Reusable UI lives under `web-ui/esp32m/ui/src/dev/lf-bros` and is exported by
  the core device index.

### Validation on 2026-08-11

- ESP32-S3 production-mode firmware compiled and linked successfully with
  ESP-IDF 6.0.2; resulting app image was `0x1aea20` bytes with 58% of the
  4-MiB app partition free.
- The source selector's protocol-probe branch and production-driver branch
  both compile. It is restored to production mode (`0`) in `main.cpp`.
- Core/project web UI compiled successfully with webpack 5.109.2.
- All 13 existing host protocol/remote-control unit tests passed.
- Setpoint selectors and offset `0x8C` still require the planned live-device
  verification; compile/unit-test success does not promote their protocol
  confidence.

### 0.1.31 live startup failure (2026-08-11)

The first live 0.1.31 production-driver boot retained the heater's switched
+5 V rail but never reached Wi-Fi. USB/JTAG serial monitoring through
`opi5b-dev-wifi` showed repeated task-watchdog reports for IDLE0 followed by a
panic/reboot around 69.8 seconds. The firmware ELF SHA was `78b190544`.

Resolved backtraces showed CPU0 alternating between `m/lfbros` in
`Lfbros::Impl::run()` at the loop delay and the main task inside
`esp_image_verify()`/`App::sketchSize()` during `App::Init` construction. The
exact defect is `vTaskDelay(pdMS_TO_TICKS(5))` with this project's
`CONFIG_FREERTOS_HZ=100`: 5 ms converts to zero ticks. Because `m/lfbros` has
priority 4 and the main task has lower priority, `vTaskDelay(0)` only yields to
equal/higher-priority ready tasks and the LF Bros loop immediately resumes.
It therefore starves both the main task and IDLE0. TWAI hardware remains
enabled, explaining why the heater continues to see a tool and keeps +5 V on.

The intended correction is a guaranteed nonzero sleep, preferably the proven
20-ms cadence (at least one tick), and production task priority 2 to match the
working probe. The 64-frame RX queue is sufficient for a 20-ms service
interval at 150 kbit/s. No protocol/timing change is needed. After rebuilding,
verify from serial that `App::Init` completes, Wi-Fi starts, no task-watchdog
messages occur, and the heater-powered CAN session remains active past 25 s.

Clarification after user review: this must not be generalized to mean every
esp32m `delay(X)` with `X < 10` makes an S3 application fail. `delay()` does
use the same floor-converting `pdMS_TO_TICKS()`, so such calls are zero-tick
yields at 100 Hz, but consequences depend on calling context. For example,
the Modbus driver's two `delay(5)` calls occur only in one-shot error recovery
between blocking Modbus/UART operations, not in a permanently ready
high-priority startup loop. Disassembly of 0.1.31 conclusively shows
`movi a10, 0` immediately before the LF Bros call to `vTaskDelay`; FreeRTOS
documents/implements zero as reschedule-only, without placing the task on the
delayed list. The LF Bros worker's priority 4 versus main priority 1 is what
makes this particular zero-delay loop fatal.

### 0.1.32 minimal scheduler fix and flash

The user approved a controlled test changing only the loop delay to
`vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(5)))`; task priority remains
4. The 0.1.32 disassembly confirms `movi a10, 1` immediately before
`vTaskDelay`, versus zero in 0.1.31. The project built and linked successfully;
app image SHA-256 is
`7ae9210bf6ea6ea62c8434c92e6230f46a7bb978b81654e7aa6a1bd90e2430b8`.

The complete bootloader/partition/initial-OTA/app set was copied with matching
hashes to `opi5b-dev-wifi:/home/orangepi/lfbros-flash-0.1.32` and programmed
over `/dev/ttyACM0` with esptool 5.0.2. Every region completed and passed
esptool hash verification. After `Hard resetting via RTS pin`, the native
USB/JTAG device disappeared and did not re-enumerate; esptool's only error was
failure to reopen the already-programmed port. Resetting the otherwise-empty
paired EHCI/OHCI host controllers (`fc800000.usb`/`fc840000.usb`) did not
recover it. Orange Pi dmesg repeatedly reports `usb usb3-port1: Cannot enable`
and `unable to enumerate USB device`; Wi-Fi/mDNS and prior IP 192.168.2.216
were not reachable at that point. A physical ESP RESET or USB unplug/replug is
needed before serial boot verification can continue.

### 0.1.33 startup event-publication guard

Inspection found a second concrete early-start hazard. `Lfbros` is deliberately
constructed before `App::Init`, but the live CAN worker could call component
setters and `EventStateChanged::publish()` while the main task was still adding
AppObject subscriptions. Component setters also publish events. EventManager's
subscriber vector is not safe for concurrent growth plus iteration. The driver
now returns from both `updateComponents()` and `publishState()` until
`App::initialized()` is true; TWAI reception, polling, claims, and the heater
startup handshake remain early and independent of the framework event graph.

0.1.33 built successfully and its app partition was flashed/verified. ROM mode
continues to enumerate reliably over native USB, but the first confirmed
physical normal boot with BOOT released still caused the USB endpoint to vanish,
no console bytes, and no Wi-Fi. Thus the publication race was real and worth
fixing but is not sufficient to explain the live failure. Re-read coredump and
CrashGuard NVS after re-entering ROM. Also test the electrical distinction that
working application CAN retains heater +5 V while the debug USB cable supplies
VBUS; ROM mode does not retain that heater rail.

### LF Bros 0.1.33 failure and HA MQTT stack correction

Direct OTA inspection showed that the SHA-prefix `78b190544` image previously
described during the LF Bros startup investigation has embedded version 0.1.30,
not 0.1.31. OTA metadata selected that higher-sequence old OTA1 instead of the
new 0.1.33 in OTA0, and the old image was in an
`InstrFetchProhibited`/PC-zero reboot loop. Once OTA selection was reset,
0.1.33 booted normally through LittleFS, Wi-Fi, and MQTT, then failed at about
6.3 seconds with an explicit FreeRTOS stack overflow in `m/ha-mqtt`. CrashGuard
then switched slots. Therefore the LF Bros early-event guard remains a valid
race prevention measure but was not the root cause of this particular reboot.

`include/esp32m/integrations/ha/mqtt.hpp` now creates `m/ha-mqtt` with an
8192-byte stack instead of 4096. This task synchronously traverses AppObjects,
constructs HA discovery JSON, and publishes discovery, so the former allocation
was insufficient for the new LF Bros entity set. Firmware 0.1.34 with this
change was written and verified in both OTA slots for recovery. Live state
showed HTTP, Wi-Fi, and MQTT healthy with at least 227 seconds uptime, well
beyond the former deterministic failure, and CrashGuard showed zero strikes in
both slots.

The project's sdkconfig has `CONFIG_FREERTOS_USE_TRACE_FACILITY` and runtime
stats disabled, so task high-water marks are unavailable without an expensive
sdkconfig/ESP-IDF rebuild. Do not enable those options merely to validate this
fix; sustained operation after completed HA discovery is sufficient. During
the recovery check CAN was physically disconnected, so LF Bros TWAI
`error-passive`, zero RX frames, and failed unacknowledged polls were expected.

### LF Bros live driver, UI, and HA acceptance

Firmware 0.1.34 was reconnected to the heater's switched +5 V supply and CAN
transceivers. It remained online beyond the rail's 25-second fallback window,
claimed the tool source, and at 69 seconds showed 68/68 valid runtime
snapshots, 1,632 RX frames, 71/71 successful TX frames, zero CAN errors/drops,
and TWAI `error-active`. The heater was fault-free and in standby-off.

The normal esp32m `state-set` path was tested with the reversible power
setpoint sequence `2000 -> 2100 -> 2000 W`. The desired state and heater
runtime mailbox agreed after each update, CAN remained error-free, and no
start command was emitted.

Retained Home Assistant discovery contains 13 LF Bros entities: climate,
power number, control-mode select, connection/fault binary sensors, and eight
telemetry sensors. Grouped state is published on
`esp32m/lf-bros-rev-eng/lfbros/state`. The HA subscriber/command path was
verified by publishing `2100` and then `2000` to
`esp32m/lf-bros-rev-eng/lfbros/power_setpoint/command`; both values propagated
through the driver and CAN into the heater runtime mailbox while OFF. Final
CAN counters were 293/293 successful TX with zero errors. This validates the
driver, reusable UI backend path, HA discovery/state, and HA power-number
command path in the heater-powered installation.

The HA climate and control-mode command paths were then live-verified while
the heater remained standby-off. JSON temperature commands 19 and 18 on
`esp32m/lf-bros-rev-eng/lfbros/climate/command` selected temperature mode and
appeared exactly in the heater runtime mailbox. Publishing `power` on
`esp32m/lf-bros-rev-eng/lfbros/control_mode/command` restored selector 1 and
the 2000 W power mailbox. Thus selector 0/temperature and selector 1/power are
now live-confirmed, not merely statically inferred.

The cumulative bus-error callback count rose once to 94 during the final test,
then remained fixed across 12 seconds while valid snapshots, RX, and TX all
continued normally; all 422 TX attempts succeeded, TX/RX error counters were
zero, TWAI was `error-active`, and the session stayed connected. Treat this as
a recovered bounded physical transient. Final desired/runtime state is power
mode, 2000 W, stored temperature 18 C, commanded OFF.

### Experimental Polr support (0.1.36)

The LF Bros driver now exposes `startPriming()` and local state-set field
`{"polr":true}` for the strong firmware-derived candidate. It is intentionally
absent from HA. The request is accepted only in a connected, source-selected,
fault-free state-1 standby with commanded OFF. The worker sends tool select
`[04 01]`, operation mode 1 `[02 01]`, then ON `[03 01]`. Stop sends `[03 00]`
and schedules normal-mode `[02 00]` only after a newer runtime snapshot reports
standby state 1. Normal Start explicitly selects mode 0 before ON, and normal
Start is blocked while Polr/restore is active.

The UI provides a confirmation-warning `Prime fuel line (Polr)` button only in
standby, disables ordinary Start and setpoint controls while Polr is selected,
and uses ordinary Stop to abort. Command state exposes `operationMode`,
`priming`, and `operationModeRestorePending`. States 21--26 are now grouped as
`priming` with neutral per-state labels rather than the previous incorrect
cooldown grouping. Final firmware/UI build 0.1.36 and all 13 host tests passed;
the app image is 1,766,048 bytes with SHA-256
`d711d8a5aab41b4228f29c559aac5acf496383b0231e4aabeeaa74207662dd1`.
Live validation must first confirm state 21 with a bounded abort, then may run
the full 80-second/500-stroke sequence with the intentionally empty line.

### Confirmed Polr pump status and stale telemetry handling (2026-08-11)

The complete 0.1.36 Polr sequence was run with an empty fuel line and works as
intended. The pump was physically operating rapidly while the last valid
runtime state was 23, so the driver now labels state 23
`polr-rapid-pumping` and treats commanded-ON Polr state 23 as an inferred
pump-on indication. The normal `0x58` frequency field remains correct for the
ordinary 0.5--8 Hz scheduler but is explicitly unavailable/null in this
special path; the driver must not report a fabricated rapid-pump frequency.

During rapid pumping, valid runtime snapshots froze while invalid snapshots
and especially TWAI bus errors increased. The driver now exposes
`heater.stale` and `heater.telemetryAgeMs`, resets HA runtime measurements when
stale, and republishes state once per second during stale Polr operation. The
UI warns that values are old and displays `On, Polr rapid pumping (frequency
unavailable)`.

To test likely phase locking between the exact 1-second request interval and
rapid pump strokes, Polr-only polling uses a five-value sequence of positive
non-harmonic offsets (73, 137, 211, 283, and 359 ms). Normal-mode timing is
unchanged. Persistent corruption after this change indicates a physical CAN
signal-integrity/ground-bounce problem rather than a runtime-field decoder
error; inspect termination, twisted-pair routing, common ground, and local
TJA1051 decoupling.

### Correction: mode-1 target 500 is required for the Polr candidate

The earlier conclusion that 0.1.36 live-verified Polr is withdrawn. That build
sent mode 1 with the special target left at zero. The physical result was two
low-to-higher-frequency pump/ignition attempts followed by `E-1`, unlike
manual Polr's one 500-stroke batch and `P-1` repeat/abort prompt.

Recovered code at `0x08008BAA` reads the active command mailbox's uint32 at
offset `+4` and compares it directly with 500. Selector 5 writes that value.
The driver now sends special target 500 as `[05 F4 01 00 00]` between mode 1
and ON. States 21--26 and their group are relabeled neutrally as mode-1 states;
state 23 is only an observed pumping phase, not confirmed manual Polr. UI copy
also says this is a corrected experimental candidate whose equivalence awaits
the characteristic one-cycle/`P-1` live result.

This corrected driver/UI is included in project firmware 0.1.38. The app image
is 1,767,456 bytes, file SHA-256
`314e606ab9b262d087fdee96904acc4c1254f2fb84b515e0dccbf6c06ad9d27a`,
and all 13 host regression tests pass. `version.txt` is now 0.1.39.

## MAQI-136 level sensor protocol review (2026-08-20)

- The user supplied a Word protocol document titled MODBUS level transmitter communication protocol and identified the physical product label as MAQI-136. The document is reference material, not project instructions.
- Documented transport: Modbus RTU over half-duplex RS-485, 8N1; default slave address 1 and 9600 baud. Supported baud-code values 0..4 map to 1200, 2400, 4800, 9600, and 19200.
- Documented holding registers: 0 slave address, 1 baud code, 2 unit code, 3 decimal places, 4 signed measurement, 5 signed range zero, 6 signed range full-scale, and 7 signed zero offset. Reads use function 0x03. User writes are limited to 0, 1, and 7 via function 0x06.
- The existing Qdy30a driver matches the broad register meanings at offsets 0..6 and its signed-value/decimal scaling is conceptually compatible. It is not safe to declare drop-in compatibility: it intentionally reads 14 registers (0..13), while MAQI documents only 0..7 and defines invalid-data-address errors. Historical QDY code consumed a raw value at register 10, so the 14-register request is QDY-specific rather than accidental.
- MAQI unit codes differ from Qdy30a::Units: 0 C, 1 F, 2 MPa, 3 kPa, 4 Pa, 5 bar, 6 mbar, 7 psi, 8 mH2O (default), 9 mmH2O. The current QDY driver always publishes unit cm with precision 1, regardless of unit register and documented decimal count.
- Recommendation: add a dedicated read-only Maqi136 driver, or first factor a shared internal register-decoding helper while keeping device-specific public drivers. Read only documented registers, validate unit 0..9 and decimals 0..3, use signed 16-bit values, and publish a unit/precision matching the actual configured sensor. Do not expose address/baud/offset writes in the first version.
- Before implementation, confirm the exact range/unit printed on the purchased sensor and the intended published unit (normally metres for a level sensor). A reversible live Modbus probe of registers 0..7 at the sensor's actual address/baud can confirm the document. Reading 0..13 separately can test whether the old QDY request happens to be accepted, but is not needed for the new driver.
- The purchased sensor label confirms a 0-50 m measurement range but does not state the protocol unit. The document does not assign a fixed native unit to this model: register 2 reports the factory-configured unit. It documents code 8, mH2O, as the generic default. For this 0-50 m unit, code 8 with two decimals is the leading expectation because raw full scale 5000 represents 50.00 and fits the signed 16-bit register, but this remains an inference until registers 2 (unit), 3 (decimals), and 6 (full scale) are read.
- No driver source changes were made during this review.

### MAQI-136 driver implementation (2026-08-20)

- Added the read-only backend in `include/esp32m/dev/maqi136.hpp` and `src/dev/maqi136.cpp`. `dev::useMaqi136(addr)` exposes device name `MAQI-136` and a distance/level sensor whose Home Assistant unit is always `m`.
- Each poll reads only documented holding registers 0..7. Register values 4..7 are treated as signed 16-bit integers, divided by the scale from register 3, then converted to metres according to register 2. Pressure conversion uses the conventional `1 mH2O = 9806.65 Pa`; psi uses `1 psi = 6894.757293168 Pa`. Codes 0/1 and decimal counts above 3 are rejected rather than misreported as level.
- Metre display precision is derived from the native measurement step. Direct mH2O preserves the encoded decimal count; mmH2O adds three metre decimals, capped at six; pressure units derive precision from their converted step.
- Added the UI plugin under `web-ui/esp32m/ui/src/dev/maqi136` and exported it from the device index. It displays level and range in metres, the sensor's native unit/decimal format, nonzero zero-offset, and Modbus address.
- Validation: the generated ESP-IDF C++23 compile command for `maqi136.cpp` completes successfully, and all new UI files pass the installed Prettier check. Workspace TypeScript compilation currently stops only on three pre-existing LF Bros/MUI prop typing errors; no MAQI TypeScript errors were reported. Installed ESLint 10 does not accept the repository's legacy `.eslintrc` configuration, so lint could not run. No live MAQI-136 hardware validation has been performed yet.

### First live MAQI-136 register result (2026-08-20)

- The driver communicates successfully with the physical sensor at Modbus address 1. The UI reports native unit `mH2O`, three decimals, level `0.000 m`, and range `0.000-5.000 m`.
- This means the decoded metadata is internally consistent with unit code 8, decimal count 3, and a full-scale register that represents 5.000 mH2O, but it conflicts with the sensor's physical 0-50 m label.
- Do not add a speculative factor-of-ten correction yet. Immerse the pressure diaphragm at a measured water depth. If approximately 1.000 m immersion reports approximately 1.000 m, the live measurement scale is plausible, but that alone cannot distinguish incorrect range metadata from an actually 0-5 m sensor carrying the wrong label; confirming the 50 m span requires vendor evidence or a known pressure/depth above 5 m. If 1.000 m reports approximately 0.100 m, the live measurement path has the same factor-of-ten mismatch and a documented model-specific correction can be considered.
- For the immersion result, record depth from the pressure diaphragm to the water surface, displayed level, and preferably the raw eight-register state. Keep the vented cable's atmospheric end dry and open to atmosphere.
- The physical sensor has a 50 m cable and laser engraving `0-50m`; this is strong evidence that the intended supplied sensor is the 50 m version, although it does not resolve whether the contradictory full-scale register is bad metadata or reflects an unexpected scaling convention.
- The MAQI UI now includes a collapsed `Protocol diagnostics` section. It exposes all eight registers R0..R7 with their documented meanings, unsigned decimal and hexadecimal values, plus signed interpretation for negative measurement/range/offset registers. This requires no backend change because the raw register array was already present in device state.

### Provisional live calibration (2026-08-21)

- At an approximately 1 m physical immersion, the live registers were R0..R7 = `1, 3, 8, 3, 11, 0, 5000, 0`; the original protocol conversion displayed `0.011 m`.
- The user requested a provisional measurement correction of x100 from this point. Deeper approximate observations (`~0.03 m` at 1.5 m and `>0.04 m` at 2 m before correction) are inconsistent with that factor and must be revisited with a controlled multi-point test.
- The driver now applies x100 only to measurement and zero offset. It separately applies x10 to the range endpoints so the encoded `0.000-5.000 m` metadata is presented as the physical label range `0.0-50.0 m`. Raw R0..R7 diagnostics remain unchanged, and the UI explicitly displays both applied factors.

### In-well multi-point calibration (2026-08-29)

- The authoritative calibration dataset is documented in `C:/work/water-well/AI_CONTEXT.md`, section 45. It covers reference water columns from 1.0 to 19.5 m and includes a repeated 8.5 m point.
- For valid `R4 > 0`, the fitted transfer function is `h_m = 0.0176676236 * R4 + 0.816811311`. Fit quality is `R² = 0.9999982`, with about 0.0072 m regression RMSE and practical absolute uncertainty about ±0.06 m after including the static-level reference uncertainty.
- Valid `R4 = 0` is the hardware under-range clamp: report `0 m` while identifying the result as under-range because the sensor cannot distinguish dry/zero from a water column up to approximately 0.82 m. Negative R4 and invalid/missing Modbus responses are not measurements; the driver invalidates the sensor state instead of publishing zero.
- The provisional measurement x100 correction was removed. The physical-label range remains 0-50 m through the separate range x10 correction, while the empirical measurement formula is stated explicitly in the UI. Raw R0..R7 diagnostics remain unchanged. The calibrated measurement and Home Assistant precision is two decimals.
- Validation after implementation: the generated ESP-IDF C++23 object target for `maqi136.cpp` compiled successfully; the MAQI UI file passed the installed Prettier check. Direct evaluation of all calibration points reproduced the references within about ±0.01 m after rounding.

### Application-owned MAQI calibration (2026-08-30)

- Removed the tested unit's affine coefficients and 0-50 m range correction
  from the reusable MAQI-136 backend and UI. An unconfigured driver again uses
  the protocol's unit/decimal conversion and R5/R6 range metadata.
- `Maqi136::calibrate(multiplier, offset)` now configures the instance-level
  conversion `metres = R4 * multiplier + offset` for positive R4 values while
  preserving R4=0 as zero/under-range. Display precision is derived from the
  configured multiplier. `setRange(min, max)` provides an explicit per-app
  override for inaccurate device range metadata.
- The backend state appends the active calibration flag and coefficients. The
  shared UI displays that runtime formula dynamically and no longer contains
  the well-level coefficients, verified span, or fixed 0.82 m text.
- The `well-level` application owns the physical unit configuration: it calls
  `calibrate(0.0176676236f, 0.816811311f)` and `setRange(0.0f, 50.0f)`.
- Validation: the MAQI backend object and well-level main object compiled, the
  two changed UI source files pass Prettier, and the well-level Webpack build
  completed. No full firmware link or hardware flash was performed.
