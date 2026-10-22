"""Host-only GCC .su/report fixtures; never build firmware or contact hardware."""

from contextlib import redirect_stderr, redirect_stdout
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "stack-report.py"
SPEC = importlib.util.spec_from_file_location("stack_report", SCRIPT)
stack_report = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stack_report)


class StackLineTests(unittest.TestCase):
    def test_gcc15_windows_cpp_signature_with_colons_spaces_and_tabs(self):
        frame = stack_report.parse_su_line(
            "C:\\work\\lib\\foo.cpp:97:12:void ns::Widget<T>::run(int) [with T = long\tint]\t384\tstatic")
        self.assertEqual(frame["source"], "C:/work/lib/foo.cpp")
        self.assertEqual(frame["function"], "void ns::Widget<T>::run(int) [with T = long\tint]")
        self.assertEqual((frame["line"], frame["column"], frame["bytes"], frame["kind"]), (97, 12, 384, "static"))

    def test_dynamic_bounded_is_distinct_from_unbounded_known_part(self):
        bounded = stack_report.parse_su_line("/a.c:2:1:vla\t128\tdynamic,bounded\n")
        unbounded = stack_report.parse_su_line("/a.c:3:1:alloc\t64\tdynamic\r\n")
        self.assertEqual(bounded["kind"], "dynamic_bounded")
        self.assertEqual(unbounded["kind"], "dynamic_unbounded")
        self.assertEqual(unbounded["bytes"], 64)

    def test_unknown_qualifier_is_retained_without_calling_it_bounded(self):
        self.assertEqual(stack_report.parse_su_line("<artificial>:0:0:init\t0\tfuture,bounded")["kind"], "unknown")

    def test_malformed_data_is_rejected(self):
        for line in ("", "foo\t4\tstatic", "a.c:1:2:f\t-1\tstatic", "a.c:1:2:f 32 static", "a.c:1:2:\t4\tstatic"):
            with self.subTest(line=line), self.assertRaises(ValueError):
                stack_report.parse_su_line(line)

    def test_windows_command_preserves_backslashes_and_quoted_spaces(self):
        command = r'"C:\Program Files\gcc.exe" -DVER=\"1\" @\"C:/a folder/cflags\" -o "esp-idf\thing\foo.cpp.obj" -c C:\src\foo.cpp'
        args = stack_report.split_command(command, windows=True)
        self.assertEqual(args[0], r"C:\Program Files\gcc.exe")
        self.assertIn("@C:/a folder/cflags", args)
        self.assertEqual(stack_report.option_value(args, "-o"), r"esp-idf\thing\foo.cpp.obj")
        self.assertEqual(args[-1], r"C:\src\foo.cpp")

    def test_posix_command_and_joined_output(self):
        args = stack_report.split_command("/tool/g++ '-DVALUE=a b' -o'obj/foo.cpp.o' -c '/src/a b.cpp'")
        self.assertEqual(args[-1], "/src/a b.cpp")
        self.assertEqual(stack_report.option_value(args, "-o"), "obj/foo.cpp.o")

    def test_language_override_and_uppercase_c(self):
        self.assertEqual(stack_report.language_for("foo.C", []), "CXX")
        self.assertEqual(stack_report.language_for("generated", ["-xc++"]), "CXX")
        self.assertEqual(stack_report.language_for("foo.c", ["-x", "assembler-with-cpp"]), "ASM")


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix=".tmp-stack-report-", dir=Path(__file__).resolve().parent)
        self.root = Path(self.temp.name)
        self.project = self.root / "project"
        self.build = self.project / "build"
        self.build.mkdir(parents=True)
        self.idf = self.root / "idf"
        self.entries = []
        self.description = {
            "project_name": "fixture", "project_version": "1.0.1", "target": "esp32s3",
            "git_revision": "v6.1", "project_path": str(self.project), "idf_path": str(self.idf),
            "app_elf": "fixture.elf", "build_components": ["main", "esp32m", "freertos"]}
        self.write_json("project_description.json", self.description)
        self.write_json("config/sdkconfig.json", {"COMPILER_OPTIMIZATION_DEBUG": True, "LIBC_PICOLIBC": True})
        self.compiler = "C:/tools/esp-15.2.0_20251204/xtensa-esp32s3-elf-gcc.exe"
        self.cxx = self.compiler.replace("gcc.exe", "g++.exe")
        self.compiler_metadata("C", self.compiler)
        self.compiler_metadata("CXX", self.cxx)
        (self.build / "fixture.elf").write_bytes(b"image-one")

    def tearDown(self):
        self.temp.cleanup()

    def write_json(self, relative, contents):
        path = self.build / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(contents), encoding="utf-8")

    def compiler_metadata(self, language, compiler, version="15.2.0"):
        path = self.build / "CMakeFiles" / "4.0.3" / ("CMake" + language + "Compiler.cmake")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('set(CMAKE_{0}_COMPILER "{1}")\nset(CMAKE_{0}_COMPILER_ID "GNU")\nset(CMAKE_{0}_COMPILER_VERSION "{2}")\n'.format(language, compiler, version), encoding="utf-8")

    def add_unit(self, name="foo.cpp", records=None, component="esp32m", flags=None, object_exists=True, metadata=True, command=False):
        source = self.project / "src" / name
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text("// fixture\n", encoding="utf-8")
        relative = "esp-idf/{0}/CMakeFiles/__idf_{0}.dir/{1}.obj".format(component, name)
        object_path = self.build / relative
        object_path.parent.mkdir(parents=True, exist_ok=True)
        if object_exists:
            object_path.write_bytes(b"object")
        args = [self.compiler if name.endswith(".c") else self.cxx,
                "-fstack-usage", "-Og"] + (flags or []) + ["-o", relative, "-c", str(source)]
        entry = {"directory": str(self.build), "file": str(source), "output": relative}
        if command:
            entry["command"] = " ".join('"{}"'.format(arg) if " " in arg else arg for arg in args)
        else:
            entry["arguments"] = args
        self.entries.append(entry)
        if metadata:
            if records is None:
                records = ["{}:10:2:void run()\t128\tstatic".format(source)]
            object_path.with_suffix(".su").write_text("\n".join(records) + ("\n" if records else ""), encoding="utf-8")
        return object_path

    def collect(self):
        self.write_json("compile_commands.json", self.entries)
        return stack_report.collect_report(self.build)

    def generate(self, **kwargs):
        self.write_json("compile_commands.json", self.entries)
        return stack_report.generate(self.build, **kwargs)

    def test_only_current_outputs_are_read_with_cpp_obj_naming(self):
        obj = self.add_unit()
        obj.with_name("unrelated.cpp.su").write_text("x:1:1:stale\t99999\tstatic\n", encoding="utf-8")
        obj.with_name("foo.su").write_text("x:1:1:wrong_basename\t99999\tstatic\n", encoding="utf-8")
        obj.with_name("foo.cpp.obj.su").write_text("x:1:1:wrong_suffix\t99999\tstatic\n", encoding="utf-8")
        report = self.collect()
        self.assertEqual([frame["bytes"] for frame in report["frames"]], [128])
        self.assertEqual(report["translation_units"][0]["metadata"], "esp-idf/esp32m/CMakeFiles/__idf_esp32m.dir/foo.cpp.su")

    def test_inactive_components_assembly_and_bootloader_are_excluded(self):
        self.add_unit()
        self.add_unit("disabled.c", component="disabled", records=["x:1:1:disabled\t9000\tstatic"])
        self.add_unit("assembler.S", records=["x:1:1:asm\t8000\tstatic"])
        self.entries.append({"directory": str(self.build), "file": str(self.project / "boot.c"), "arguments": [self.compiler, "-fstack-usage", "-o", "bootloader/boot.c.obj", "-c", "boot.c"]})
        report = self.collect()
        self.assertEqual(report["summary"]["expected_translation_units"], 1)
        self.assertEqual(report["summary"]["ignored"], {"assembly_translation_units": 1, "bootloader_translation_units": 1, "inactive_component_translation_units": 1})

    def test_disabled_flag_does_not_reuse_stale_sidecar(self):
        self.add_unit(flags=["-fno-stack-usage"])
        report = self.collect()
        self.assertEqual(report["frames"], [])
        self.assertEqual(report["translation_units"][0]["status"], "stack_usage_disabled")

    def test_missing_sidecar_and_object_are_visible(self):
        self.add_unit("no-metadata.c", metadata=False)
        self.add_unit("no-object.cpp", object_exists=False)
        report = self.collect()
        self.assertEqual(report["summary"]["expected_translation_units"], 2)
        self.assertEqual(report["summary"]["reported_translation_units"], 0)
        self.assertFalse(report["summary"]["source_metadata_complete"])
        self.assertEqual({unit["status"] for unit in report["translation_units"]}, {"missing_metadata", "missing_object"})

    def test_empty_data_only_translation_unit_is_valid(self):
        self.add_unit(records=[])
        report = self.collect()
        self.assertEqual(report["summary"]["frames"], 0)
        self.assertEqual(report["summary"]["reported_translation_units"], 1)
        self.assertTrue(report["summary"]["source_metadata_complete"])

    def test_counts_sorting_threshold_and_unknown_qualifier(self):
        self.add_unit(records=["x.c:1:1:small\t32\tstatic", "x.c:2:1:bounded\t2000\tdynamic,bounded",
                               "x.c:3:1:unbounded\t160\tdynamic", "x.c:4:1:unknown\t80\tfuture"])
        report = self.collect()
        summary = report["summary"]
        self.assertEqual([frame["bytes"] for frame in report["frames"]], [2000, 160, 80, 32])
        self.assertEqual((summary["static_frames"], summary["dynamic_frames"], summary["bounded_dynamic_frames"], summary["unbounded_dynamic_frames"], summary["unknown_qualifier_frames"]), (1, 2, 1, 1, 1))
        self.assertEqual(summary["frames_exceeding_warning"], 1)
        self.assertFalse(summary["source_metadata_complete"])

    def test_malformed_records_do_not_hide_other_frames(self):
        self.add_unit(records=["x.c:1:1:ok\t32\tstatic", "this is not stack metadata"])
        report = self.collect()
        self.assertEqual(report["summary"]["frames"], 1)
        self.assertEqual(report["translation_units"][0]["status"], "parse_errors")
        self.assertEqual(report["translation_units"][0]["errors"][0]["line"], 2)

    def test_response_file_flags_are_expanded(self):
        self.add_unit(flags=[])
        self.entries[0]["arguments"].remove("-fstack-usage")
        self.entries[0]["arguments"].remove("-Og")
        response = self.build / "a folder" / "flags"
        response.parent.mkdir()
        response.write_text("-fstack-usage -Os -fstack-protector-strong\n", encoding="utf-8")
        self.entries[0]["arguments"].insert(1, "@" + str(response))
        report = self.collect()
        self.assertEqual(report["summary"]["reported_translation_units"], 1)
        profile = report["identity"]["profile"]["compile_profiles"][0]
        self.assertEqual(profile["optimization"], "-Os")
        self.assertIn("-fstack-protector-strong", profile["codegen_options"])

    def test_missing_response_file_blocks_comparison_profile(self):
        self.add_unit(flags=["@missing-flags"])
        report = self.collect()
        self.assertFalse(report["identity"]["comparison_profile_complete"])
        self.assertTrue(any("Cannot read response file" in value for value in report["warnings"]))

    def test_response_cycle_is_bounded(self):
        self.add_unit(flags=["@flags"])
        (self.build / "flags").write_text("@flags", encoding="utf-8")
        report = self.collect()
        self.assertTrue(any("cycle/depth" in value for value in report["warnings"]))

    def test_windows_object_separator_maps_to_sidecar(self):
        self.add_unit()
        self.entries[0]["output"] = self.entries[0]["output"].replace("/", "\\")
        self.assertEqual(self.collect()["summary"]["reported_translation_units"], 1)

    def test_custom_dump_names_are_a_visible_gap(self):
        self.add_unit(flags=["-dumpbase", "custom"])
        report = self.collect()
        self.assertEqual(report["translation_units"][0]["status"], "unsupported_dump_options")
        self.assertEqual(report["frames"], [])

    def test_current_database_duplicate_outputs_are_not_double_counted(self):
        self.add_unit()
        self.entries.append(copy.deepcopy(self.entries[0]))
        report = self.collect()
        self.assertEqual(report["summary"]["frames"], 1)
        self.assertEqual(report["summary"]["ignored"]["duplicate_compile_entries"], 1)

    def test_idf_source_locations_are_portable(self):
        self.add_unit(records=["{}:9:1:void idf_function()\t64\tstatic".format(self.idf / "components" / "freertos" / "tasks.c")])
        self.assertEqual(self.collect()["frames"][0]["source"], "(IDF)/components/freertos/tasks.c")

    def test_shared_component_and_prefix_maps_remain_stable_when_workspace_moves(self):
        original = self.root / "original"
        moved = self.root / "moved"
        def names(root):
            project, idf = root / "projects" / "scratch", root / "idf"
            source = root / "core" / "esp32m" / "src" / "driver.cpp"
            args = ["-Og", "-fno-inline", "-fmacro-prefix-map={}=.".format(project),
                    "-fmacro-prefix-map={}=/IDF".format(idf)]
            return (stack_report.source_name(source, project, idf),
                    stack_report.codegen_profile(args, project, idf))
        self.assertEqual(names(original), names(moved))
        self.assertEqual(names(original)[0], "../../core/esp32m/src/driver.cpp")

    def test_growth_survives_line_shift_and_noop_rerun(self):
        obj = self.add_unit()
        initial, _, _ = self.generate()
        self.assertEqual(initial["comparison"]["status"], "unavailable")
        obj.with_suffix(".su").write_text("{}:25:2:void run()\t384\tstatic\n".format(self.project / "src" / "foo.cpp"), encoding="utf-8")
        (self.build / "fixture.elf").write_bytes(b"image-two")
        grown, text_path, json_path = self.generate()
        self.assertEqual(grown["comparison"]["growth"][0]["delta_bytes"], 256)
        self.assertEqual(grown["comparison"]["matched_frames"], 1)
        before = (text_path.read_bytes(), json_path.read_bytes(), (self.build / "stack-usage.previous.json").read_bytes())
        repeated, _, _ = self.generate()
        self.assertEqual(repeated["comparison"], grown["comparison"])
        after = (text_path.read_bytes(), json_path.read_bytes(), (self.build / "stack-usage.previous.json").read_bytes())
        self.assertEqual(before, after)
        self.assertEqual(json.loads(after[2])["artifact"]["fingerprint"], initial["artifact"]["fingerprint"])

    def test_comparison_rejects_target_compiler_or_optimization_changes(self):
        self.add_unit()
        initial = self.collect()
        for section in ("target", "compiler", "optimization", "idf"):
            with self.subTest(section=section):
                description = copy.deepcopy(self.description)
                self.compiler_metadata("CXX", self.cxx)
                self.entries[0]["arguments"][2] = "-Og"
                if section == "target":
                    description["target"] = "esp32"
                elif section == "compiler":
                    self.compiler_metadata("CXX", self.cxx, "14.2.0")
                elif section == "optimization":
                    self.entries[0]["arguments"][2] = "-Os"
                else:
                    description["git_revision"] = "v6.0"
                self.write_json("project_description.json", description)
                current = self.collect()
                result = stack_report.compare_reports(current, initial, "saved")
                self.assertEqual(result["status"], "incompatible")
                self.assertEqual(result["growth"], [])

    def test_compiler_version_and_effective_profile_are_part_of_identity(self):
        self.add_unit()
        initial = self.collect()
        self.assertTrue(initial["identity"]["comparison_profile_complete"])
        self.assertEqual(initial["identity"]["compilers"]["CXX"][0]["version"], "15.2.0")
        self.compiler_metadata("CXX", self.cxx, "15.2.1")
        version_changed = self.collect()
        self.assertNotEqual(initial["identity"]["comparison_key"], version_changed["identity"]["comparison_key"])
        self.entries[0]["arguments"].insert(2, "-Os")
        changed = self.collect()
        self.assertNotEqual(initial["identity"]["comparison_key"], changed["identity"]["comparison_key"])

    def test_unknown_compiler_version_disables_comparison(self):
        self.add_unit()
        (self.build / "CMakeFiles" / "4.0.3" / "CMakeCXXCompiler.cmake").unlink()
        initial = self.collect()
        self.assertFalse(initial["identity"]["comparison_profile_complete"])
        self.assertEqual(stack_report.compare_reports(initial, initial, "saved")["status"], "unavailable")

    def test_swapped_translation_unit_profiles_are_incompatible_even_with_same_set(self):
        self.add_unit("size.cpp", flags=["-Os"])
        self.add_unit("speed.cpp", flags=["-O2"])
        initial = self.collect()
        self.entries[0]["arguments"][3] = "-O2"
        self.entries[1]["arguments"][3] = "-Os"
        current = self.collect()
        self.assertEqual(initial["identity"]["profile"]["compile_profiles"], current["identity"]["profile"]["compile_profiles"])
        self.assertEqual(initial["identity"]["comparison_key"], current["identity"]["comparison_key"])
        self.assertEqual(stack_report.compare_reports(current, initial, "saved")["status"], "incompatible")

    def test_added_translation_unit_keeps_existing_function_growth_comparable(self):
        obj = self.add_unit("existing.cpp")
        initial = self.collect()
        obj.with_suffix(".su").write_text("{}:25:2:void run()\t384\tstatic\n".format(self.project / "src" / "existing.cpp"), encoding="utf-8")
        # A new file is allowed its own profile; there is no old frame with
        # which its functions could be compared.
        self.add_unit("new.cpp", flags=["-Os"], records=["new.cpp:1:1:void added()\t640\tstatic"])
        current = self.collect()
        compared = stack_report.compare_reports(current, initial, "saved")
        self.assertEqual(compared["status"], "compared")
        self.assertEqual(compared["growth"][0]["delta_bytes"], 256)
        self.assertEqual(compared["added_translation_units"], 1)
        self.assertEqual(compared["unmatched_frames"], 1)
        self.assertEqual(compared["matched_frames"], 1)

    def test_removed_translation_unit_does_not_disable_remaining_comparison(self):
        self.add_unit("retained.cpp")
        self.add_unit("removed.cpp")
        initial = self.collect()
        self.entries.pop()
        current = self.collect()
        compared = stack_report.compare_reports(current, initial, "saved")
        self.assertEqual(compared["status"], "compared")
        self.assertEqual(compared["removed_translation_units"], 1)
        self.assertEqual(compared["removed_frames"], 1)
        self.assertEqual(compared["matched_frames"], 1)

    def test_replaced_tu_with_same_object_and_header_frame_is_unmatched(self):
        obj = self.add_unit("old.cpp", records=["shared.h:1:1:void helper()\t64\tstatic"])
        initial = self.collect()
        # The object output and header signature survive a source replacement;
        # that does not make the translation units comparable.
        self.entries[0]["file"] = str(self.project / "src" / "replacement.cpp")
        obj.with_suffix(".su").write_text("shared.h:1:1:void helper()\t384\tstatic\n", encoding="utf-8")
        current = self.collect()
        compared = stack_report.compare_reports(current, initial, "saved")
        self.assertEqual(compared["status"], "compared")
        self.assertEqual(compared["growth"], [])
        self.assertEqual(compared["matched_frames"], 0)
        self.assertEqual(compared["unmatched_frames"], 1)
        self.assertEqual(compared["removed_frames"], 1)

    def test_noinline_flag_changes_disable_automatic_frame_comparison(self):
        self.add_unit()
        initial = self.collect()
        self.entries[0]["arguments"].insert(3, "-fno-inline")
        current = self.collect()
        self.assertIn("-fno-inline", current["translation_units"][0]["compile_profile"]["codegen_options"])
        self.assertEqual(stack_report.compare_reports(current, initial, "saved")["status"], "incompatible")

    def test_explicit_baseline_and_custom_output_prefix(self):
        obj = self.add_unit()
        baseline, _, baseline_path = self.generate(output_prefix=self.root / "accepted")
        obj.with_suffix(".su").write_text("{}:12:2:void run()\t192\tstatic\n".format(self.project / "src" / "foo.cpp"), encoding="utf-8")
        current, text_path, json_path = self.generate(baseline_path=baseline_path)
        self.assertEqual(current["comparison"]["growth"][0]["delta_bytes"], 64)
        self.assertEqual(current["comparison"]["baseline_artifact"], baseline["artifact"])
        self.assertEqual(text_path, self.build / "stack-usage.txt")
        self.assertEqual(json_path, self.build / "stack-usage.json")

    def test_qualifier_changes_and_duplicate_functions_are_not_fake_growth(self):
        self.add_unit()
        initial = self.collect()
        current = copy.deepcopy(initial)
        current["frames"][0]["kind"] = "dynamic_unbounded"
        result = stack_report.compare_reports(current, initial, "saved")
        self.assertEqual(result["changed_kind_frames"], 1)
        self.assertEqual(result["growth"], [])
        current = copy.deepcopy(initial)
        current["frames"].append(copy.deepcopy(current["frames"][0]))
        result = stack_report.compare_reports(current, initial, "saved")
        self.assertEqual(result["matched_frames"], 0)

    def test_text_disclaims_task_sizes_and_marks_dynamic_known_portion(self):
        self.add_unit(records=["x:1:1:vla\t4096\tdynamic"])
        report, text_path, _ = self.generate(top=1)
        text = text_path.read_text(encoding="utf-8")
        self.assertIn("PER-TASK USAGE/RECOMMENDATIONS: UNAVAILABLE", text)
        self.assertIn("only the known portion", text)
        self.assertIn("Whole-program coverage: INCOMPLETE", text)
        self.assertIn("dynamic_unbounded", text)
        self.assertNotIn("recommended task size", text)

    def test_own_functions_are_visible_even_when_sdk_dominates_top_list(self):
        self.add_unit("sdk.c", component="freertos", records=["x:1:1:sdk_big\t9999\tstatic"])
        self.add_unit("main.cpp", component="main", records=["x:1:1:my_main\t120\tstatic"])
        self.add_unit("esp.cpp", records=["x:1:1:my_driver\t80\tstatic"])
        _, path, _ = self.generate(top=1)
        text = path.read_text(encoding="utf-8")
        self.assertIn("Largest esp32m/application function frames (1 of 2):", text)
        own_section = text.split("Largest esp32m/application function frames", 1)[1].split("Limits:", 1)[0]
        self.assertIn("my_main", own_section)
        self.assertNotIn("sdk_big", own_section)

    def test_missing_database_error_is_clear_and_does_not_overwrite_report(self):
        self.add_unit()
        _, _, path = self.generate()
        previous_bytes = path.read_bytes()
        (self.build / "compile_commands.json").unlink()
        with redirect_stderr(io.StringIO()) as error:
            status = stack_report.main(["--build-dir", str(self.build)])
        self.assertEqual(status, 2)
        self.assertIn("compile_commands.json", error.getvalue())
        self.assertEqual(path.read_bytes(), previous_bytes)

    def test_cli_advisory_gaps_succeed_and_report_paths(self):
        self.add_unit(metadata=False)
        self.write_json("compile_commands.json", self.entries)
        with redirect_stdout(io.StringIO()) as output:
            status = stack_report.main(["--build-dir", str(self.build), "--top", "2", "--frame-warning-bytes", "512"])
        self.assertEqual(status, 0)
        self.assertIn("0/1 C/C++ TUs", output.getvalue())
        self.assertTrue((self.build / "stack-usage.json").is_file())


if __name__ == "__main__":
    unittest.main()
