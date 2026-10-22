# SM10 protocol tests

Run from the workspace root:

```sh
node core/esp32m/tests/sm10-protocol/run.mjs
```

The runner compiles the actual dependency-free production protocol header as
C++17 with `-Wall -Wextra -Werror`. It selects `CXX`, a known Windows MinGW
installation, or `g++`. The executable and temporary build directory are removed
afterward. No ESP32, serial port, debug probe or meter is accessed.

The fixed reply vectors come from the SM10 API qualification and authorized RTC
setting captures on 2026-09-24. They exercise actual meter packets independently
of the encoder: EB identity, fine total, status, daily history, invalid RTC,
successful RTC setting/readback and its `FE`-prefixed acknowledgment.

Additional cases cover partial/noisy/corrupt input, command/address/sequence and
length validation, BCD and Q14 limits, sign-magnitude temperature, calendar/leap
year and weekday validation, the transmit command allowlist, ID 235 collision,
baud selectors, coupled configuration/total byte layout, preserved upper
address bytes, total resolution/range, all four history bank sizes and order,
and standard Modbus flow decoding/CRC, including the whole-L/h wire unit
confirmed by producer arithmetic and Influx comparison on 2026-09-29.
They do not claim hardware qualification
of ID/baud/total writes or long-term clock accuracy.
