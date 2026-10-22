"""Compile changed production sources with an existing ESP-IDF compile database.

Only isolated objects under this test directory are written; application build
artifacts, version files and devices are untouched. Run from any directory.
"""
import argparse
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
CORE = HERE.parent.parent
parser = argparse.ArgumentParser()
parser.add_argument("--database", type=Path, default=CORE.parent.parent /
                    "projects/cdhx-test/build/compile_commands.json")
parser.add_argument("--source", action="append", help="Recheck selected project-relative sources only")
args = parser.parse_args()
database = json.loads(args.database.read_text(encoding="utf-8"))
entry = next(e for e in database if e["file"].replace("\\", "/").endswith("src/dev/device.cpp"))
prefix = entry["command"].rsplit(" -o ", 1)[0]
build = HERE / ".build-compile"
build.mkdir(exist_ok=True)
sources = ["src/dev/device.cpp", "src/dev/ntc.cpp", "src/dev/thermostat/controller.cpp",
           "src/dev/thermostat/thermostat.cpp", "src/dev/cdhx.cpp", "src/dev/lf-bros.cpp",
           "src/dev/opentherm/opentherm.cpp",
           "src/net/mqtt.cpp", "src/integrations/ha/ha.cpp"]
if args.source:
    sources = args.source
ha_header = build / "ha-mqtt.cpp"
ha_header.write_text('#include "esp32m/integrations/ha/mqtt.hpp"\n', encoding="utf-8")
direct = build / "direct-actuators.cpp"
direct.write_text('''#include "esp32m/dev/cdhx.hpp"
#include "esp32m/dev/lf-bros.hpp"
#include "esp32m/dev/opentherm.hpp"
#include "esp32m/dev/thermostat.hpp"
#include <type_traits>
using namespace esp32m::dev;
static_assert(std::is_base_of_v<thermostat::Actuator, Cdhx>);
static_assert(std::is_base_of_v<thermostat::Actuator, Lfbros>);
static_assert(std::is_base_of_v<thermostat::Actuator, OpenthermMaster>);
void compileDirectAttachment(Cdhx& cdhx, Lfbros& lfbros, OpenthermMaster& boiler) {
  thermostat::Actuator* actuator = &cdhx;
  actuator = &lfbros;
  actuator = &boiler;
  (void)actuator;
  (void)useThermostat(cdhx);
  (void)useThermostat(lfbros);
  (void)useThermostat(boiler);
}
''', encoding="utf-8")
checks = sources + [str(ha_header), str(direct)]
failures = []
for index, relative in enumerate(checks):
    source = CORE / relative
    output = build / f"{index}.obj"
    command = f'{prefix} -o "{output}" -c "{source}"'
    result = subprocess.run(command, cwd=entry["directory"], capture_output=True, text=True)
    if result.returncode:
        failures.append(relative)
        print(f"FAIL {relative}\n{result.stdout}\n{result.stderr}", flush=True)
    else:
        print(f"PASS {relative}", flush=True)
print(f"ESP-IDF compilation: {len(checks) - len(failures)}/{len(checks)} sources passed")
raise SystemExit(bool(failures))
