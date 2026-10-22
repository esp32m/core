# Command-line scripts

## Stack usage reports

Function-frame reports are optional and disabled by default. Applications
generate `stack-usage.txt` and `stack-usage.json` in their build directory only
when `CONFIG_ESP32M_STACK_REPORT=y` is explicitly enabled. The shared hook runs
`stack-report.py` after linking and also exposes `esp32m_stack_report` as an
explicit build target. No SDK or application CMake edits are required.

The script uses Python's standard library. From the outer workspace root:

```powershell
python core/esp32m/scripts/stack-report.py --build-dir projects/my-app/build
python core/esp32m/scripts/stack-report.py --build-dir projects/my-app/build --baseline releases/accepted-stack-usage.json
```

`--frame-warning-bytes` changes the advisory per-function threshold (default
1024); `--top` changes the number of largest/grown frames shown in text (default
50). JSON contains all reported frames. `--output-prefix` writes a separate
report pair without replacing the default build report.

Automatic comparison uses the prior distinct build; unchanged-image refreshes
keep their existing comparison. Retain an accepted release's JSON explicitly
for stable release comparisons. Different targets, compilers or effective
optimization/code-generation profiles are marked incompatible rather than
compared as the same build profile.

These are per-function compiler reports. They do not provide whole-task
minimum/maximum stack usage or safe allocation recommendations.

## HTTP OTA upload

`ota-upload.py` sends an application firmware binary to an esp32m device running
OTA-enabled firmware. It uses Python 3's standard library; no packages need to
be installed. For example, from the workspace root:

```powershell
python core/esp32m/scripts/ota-upload.py esp32m.local projects/my-app/build/my-app.bin
python core/esp32m/scripts/ota-upload.py 192.168.1.50 projects/my-app/build/my-app.bin
```

Use the application's `.bin` file, rather than a bootloader, partition table,
or merged flash image. The existing firmware must expose `/ota-upload`, have
an available OTA partition, and allow direct file uploads (vendor-only OTA
mode rejects them).

The host argument also accepts a port (`esp32m.local:8080`) or an HTTP(S)
origin (`https://esp32m.local`). IPv6 addresses can be bare or bracketed;
include brackets when specifying a port (`[fd00::1]:8080`). HTTPS uses normal
certificate verification.

Options:

- `--timeout 180`: socket timeout in seconds (default: 120).
- `--username admin`: HTTP Basic authentication, with an interactive password
  prompt. For unattended use, add `--password VALUE`; command-line arguments
  may be visible in process listings and shell history.
- `--help`: show usage.

The script streams one raw `POST /ota-upload` with an explicit content length.
It reports success only after HTTP 200 with an `OK` body, which confirms image
validation and selection for the next boot. The MCU then reboots automatically;
the script does not verify the new firmware after reboot. Redirects and uploads
are never retried automatically. If an upload is interrupted or its result is
unconfirmed, check the device's state before retrying.

Exit codes: `0` accepted, `1` file/upload error, `2` invalid arguments,
`130` interrupted.

Local integration tests, from the `core/esp32m` directory:

```powershell
python -m unittest discover -s tests/ota-upload -p "test_*.py" -v
```
