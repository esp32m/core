"""Exercise the real project hook with a small, device-free IDF build model.

Run: python -B -m unittest discover -s core/esp32m/tests/stack-build -v
The fixture mirrors IDF's late EXECUTABLE property and post-ELF bin dependency.
It needs CMake and Ninja (or CMake's default platform build tool), not ESP-IDF.
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


COMPONENT = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake")

CMAKELISTS = r"""
cmake_minimum_required(VERSION 3.22)
project(stack_hook NONE)
add_custom_target(__idf_build_target)
function(idf_build_get_property out property)
    if(ARGN STREQUAL "GENERATOR_EXPRESSION")
        set(value "$<TARGET_PROPERTY:__idf_build_target,${property}>")
    else()
        get_property(value TARGET __idf_build_target PROPERTY "${property}")
    endif()
    set(${out} "${value}" PARENT_SCOPE)
endfunction()
function(idf_build_set_property property value)
    if(ARGN STREQUAL "APPEND")
        set_property(TARGET __idf_build_target APPEND PROPERTY "${property}" "${value}")
    else()
        set_property(TARGET __idf_build_target PROPERTY "${property}" "${value}")
    endif()
endfunction()
if(HAS_POST_ELF_API)
    function(idf_build_add_post_elf_dependency elf target)
        if(NOT TARGET "${target}")
            message(FATAL_ERROR "post-ELF dependency target does not exist")
        endif()
        idf_build_set_property("__POST_ELF_DEPS_${elf}" "${target}" APPEND)
    endfunction()
endif()
idf_build_set_property(BUILD_DIR "${CMAKE_BINARY_DIR}")
idf_build_set_property(PYTHON "${TEST_PYTHON}")
idf_build_set_property(BOOTLOADER_BUILD "${TEST_BOOTLOADER}")
idf_build_set_property(ESP_TEE_BUILD "${TEST_TEE}")
set(CMAKE_C_COMPILER_ID "${TEST_COMPILER}")
set(CMAKE_CXX_COMPILER_ID "${TEST_COMPILER}")
include("${CMAKE_CURRENT_SOURCE_DIR}/component/project_include.cmake")
idf_build_get_property(c_flags C_COMPILE_OPTIONS)
idf_build_get_property(cxx_flags CXX_COMPILE_OPTIONS)
idf_build_get_property(all_flags COMPILE_OPTIONS)
file(WRITE "${CMAKE_BINARY_DIR}/flags.txt" "${c_flags}\n${cxx_flags}\n${all_flags}\n")

# The real ELF does not exist when component project_include hooks run.
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/stack_hook.elf"
    COMMAND "${TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/fake-build.py"
        link "${CMAKE_BINARY_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/main.c" VERBATIM)
add_custom_target(fake_link DEPENDS "${CMAKE_BINARY_DIR}/stack_hook.elf")
add_executable(fake_executable IMPORTED GLOBAL)
set_target_properties(fake_executable PROPERTIES
    IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/stack_hook.elf")
idf_build_set_property(EXECUTABLE fake_executable)
idf_build_get_property(post_deps __POST_ELF_DEPS_stack_hook.elf)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/firmware.bin"
    COMMAND "${TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/fake-build.py"
        binary "${CMAKE_BINARY_DIR}"
    DEPENDS "${CMAKE_BINARY_DIR}/stack_hook.elf" ${post_deps} VERBATIM)
add_custom_target(app ALL DEPENDS "${CMAKE_BINARY_DIR}/firmware.bin")
"""

FAKE_BUILD = r"""
from pathlib import Path
import sys
phase, directory = sys.argv[1:]
build = Path(directory)
with (build / 'events.txt').open('a', encoding='utf-8') as f:
    f.write(phase + '\n')
(build / ('stack_hook.elf' if phase == 'link' else 'firmware.bin')).write_text(phase)
"""

REPORTER = r"""
import argparse
import json
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=Path, required=True)
build = parser.parse_args().build_dir
if not (build / 'stack_hook.elf').exists():
    raise SystemExit('report ran before ELF existed')
with (build / 'events.txt').open('a', encoding='utf-8') as f:
    f.write('report\n')
events = (build / 'events.txt').read_text().splitlines()
(build / 'stack-usage.txt').write_text('report run ' + str(events.count('report')))
(build / 'stack-usage.json').write_text(json.dumps({'runs': events.count('report')}))
"""


@unittest.skipUnless(CMAKE, "CMake is required")
class StackBuildHookTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="esp32m stack build ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source with spaces"
        self.build = self.root / "build with spaces"
        scripts = self.source / "component" / "scripts"
        scripts.mkdir(parents=True)
        shutil.copyfile(COMPONENT / "project_include.cmake", scripts.parent / "project_include.cmake")
        shutil.copyfile(COMPONENT / "scripts" / "stack-analysis.cmake", scripts / "stack-analysis.cmake")
        (scripts / "stack-report.py").write_text(REPORTER, encoding="utf-8")
        (self.source / "CMakeLists.txt").write_text(CMAKELISTS, encoding="utf-8")
        (self.source / "fake-build.py").write_text(FAKE_BUILD, encoding="utf-8")
        (self.source / "main.c").write_text("int main(void) { return 0; }\n", encoding="utf-8")

    def run_command(self, *args):
        result = subprocess.run(args, capture_output=True, text=True, env=os.environ.copy())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def configure(self, **options):
        defaults = dict(CONFIG_ESP32M_STACK_REPORT="ON", HAS_POST_ELF_API="ON",
                        TEST_COMPILER="GNU", TEST_BOOTLOADER="OFF", TEST_TEE="OFF")
        defaults.update(options)
        args = [CMAKE, "-S", str(self.source), "-B", str(self.build),
                "-DTEST_PYTHON=" + sys.executable]
        ninja = shutil.which("ninja")
        if ninja:
            args += ["-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja]
        args += ["-D" + key + "=" + value for key, value in defaults.items()]
        return self.run_command(*args)

    def build_target(self, target=None):
        args = [CMAKE, "--build", str(self.build)]
        if target:
            args += ["--target", target]
        self.run_command(*args)

    def events(self):
        return (self.build / "events.txt").read_text().splitlines()

    def test_global_flags_post_link_order_incremental_and_manual_target(self):
        self.configure()
        self.assertEqual((self.build / "flags.txt").read_text().splitlines(),
                         ["-fstack-usage", "-fstack-usage", ""])
        self.build_target()
        self.assertEqual(self.events(), ["link", "report", "binary"])
        elf_time = (self.build / "stack_hook.elf").stat().st_mtime_ns
        bin_time = (self.build / "firmware.bin").stat().st_mtime_ns
        self.build_target()
        self.assertEqual(self.events(), ["link", "report", "binary", "report"])
        self.assertEqual((self.build / "stack_hook.elf").stat().st_mtime_ns, elf_time)
        self.assertEqual((self.build / "firmware.bin").stat().st_mtime_ns, bin_time)
        reporter = self.source / "component/scripts/stack-report.py"
        reporter.write_text(REPORTER + "\n# parser revision\n", encoding="utf-8")
        self.build_target("esp32m_stack_report")
        self.assertEqual(self.events().count("link"), 1)
        self.assertEqual(self.events().count("binary"), 1)
        self.assertEqual(json.loads((self.build / "stack-usage.json").read_text())["runs"], 3)
        (self.source / "main.c").write_text("int main(void) { return 1; }\n", encoding="utf-8")
        self.build_target()
        self.assertEqual(self.events()[-3:], ["link", "report", "binary"])

    def test_feature_disabled_has_no_flags_or_report(self):
        self.configure(CONFIG_ESP32M_STACK_REPORT="OFF")
        self.build_target()
        self.assertEqual((self.build / "flags.txt").read_text(), "\n\n\n")
        self.assertEqual(self.events(), ["link", "binary"])
        self.assertFalse((self.build / "stack-usage.txt").exists())

    def test_non_gnu_compiler_is_skipped(self):
        self.assertIn("requires a GNU", self.configure(TEST_COMPILER="Clang"))
        self.build_target()
        self.assertEqual((self.build / "flags.txt").read_text(), "\n\n\n")
        self.assertNotIn("report", self.events())

    def test_bootloader_is_skipped(self):
        self.configure(TEST_BOOTLOADER="ON")
        self.build_target()
        self.assertEqual((self.build / "flags.txt").read_text(), "\n\n\n")
        self.assertNotIn("report", self.events())

    def test_tee_is_skipped(self):
        self.configure(TEST_TEE="ON")
        self.build_target()
        self.assertEqual((self.build / "flags.txt").read_text(), "\n\n\n")
        self.assertNotIn("report", self.events())

    def test_older_idf_without_post_elf_api(self):
        self.configure(HAS_POST_ELF_API="OFF")
        self.build_target()
        self.assertEqual(self.events()[0], "link")
        self.assertIn("report", self.events())
        self.assertTrue((self.build / "stack-usage.txt").exists())


if __name__ == "__main__":
    unittest.main()
