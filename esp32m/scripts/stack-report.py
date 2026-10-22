#!/usr/bin/env python3
"""Report GCC function stack frames for the current ESP-IDF application build.

This is an advisory per-function report, not a task stack bound calculator.
Only .su files derived from current C/C++ compile database outputs are read.
No compiler, firmware, debugger, or device command is executed.
"""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import sys
import tempfile


SCHEMA_VERSION = 1
LIMITATIONS = [
    "Function frames exclude callees: no per-task minimum, maximum, or safe stack allocation can be inferred from this report.",
    "Unbounded dynamic entries show only the compiler's known portion, not an upper bound.",
    "Only current application C/C++ compile database outputs with stack metadata are covered; unused/disabled component sidecars are not scanned.",
    "ROM, precompiled libraries (including compiler/libc libraries), assembly, separate bootloader and external builds are not analyzed.",
    "Compiled functions can be removed by the linker; this report is not a list of live functions in the ELF. LTO can move or change frames and metadata.",
    "Callbacks, virtual calls, recursion, interrupt/context overhead, and complete call chains are not resolved.",
    "A matching .su path is not proof of metadata freshness. Compiler caches may omit or retain sidecars; use a fresh build without ccache if coverage/freshness is uncertain.",
]


class ReportError(Exception):
    """An input/output error that can be explained without a traceback."""


def normalized(path):
    return str(path).replace("\\", "/")


def path_key(path):
    value = normalized(path)
    return value.casefold() if re.match(r"^[A-Za-z]:/", value) else value


def absolute_path(path, base):
    value = normalized(path)
    # Path on Windows recognizes drive-qualified paths. On POSIX retain them
    # lexically, rather than pretending that C:/... is relative to the build.
    if re.match(r"^[A-Za-z]:/", value) and os.name != "nt":
        return Path(value)
    result = Path(value)
    return result if result.is_absolute() else Path(base) / result


def relative_name(path, base):
    value, root = normalized(path), normalized(base).rstrip("/")
    if path_key(value) == path_key(root):
        return "."
    if path_key(value).startswith(path_key(root) + "/"):
        return value[len(root) + 1:]
    return value


def source_name(path, project_path, idf_path):
    """Give source locations stable names when project/build roots move."""
    idf_relative = relative_name(path, idf_path)
    if idf_relative != normalized(path):
        return "(IDF)/" + idf_relative
    try:
        return normalized(os.path.relpath(path, project_path))
    except ValueError:  # Different Windows drives cannot be made relative.
        return normalized(path)


def read_json(path, required=True):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except FileNotFoundError as error:
        if not required:
            return None
        raise ReportError("Missing {}. Configure/build the ESP-IDF application first.".format(path)) from error
    except (OSError, ValueError) as error:
        raise ReportError("Cannot read {}: {}".format(path, error)) from error


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("utf-8")).hexdigest()


def file_digest(path):
    try:
        value = hashlib.sha256()
        with Path(path).open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                value.update(chunk)
        return value.hexdigest()
    except OSError as error:
        raise ReportError("Cannot hash {}: {}".format(path, error)) from error


def split_command(command, windows=False):
    """Split compiler arguments without losing Windows path backslashes.

    CMake's Windows compile database can quote response files as @\"path\".
    Normalize only that form, not escaped quotes inside macro definitions.
    """
    if not windows:
        return shlex.split(command, posix=True)
    command = re.sub(r'@\\"(.*?)\\"', r'@"\1"', command)
    tokens, token = [], []
    quoted = False
    index = 0
    while index < len(command):
        char = command[index]
        if char.isspace() and not quoted:
            if token:
                tokens.append("".join(token))
                token = []
            index += 1
            continue
        if char == "\\":
            end = index
            while end < len(command) and command[end] == "\\":
                end += 1
            count = end - index
            if end < len(command) and command[end] == '"':
                token.append("\\" * (count // 2))
                if count % 2:
                    token.append('"')
                else:
                    quoted = not quoted
                index = end + 1
                continue
            token.append("\\" * count)
            index = end
            continue
        if char == '"':
            quoted = not quoted
        else:
            token.append(char)
        index += 1
    if quoted:
        raise ValueError("unterminated command quote")
    if token:
        tokens.append("".join(token))
    return tokens


def command_arguments(entry):
    if isinstance(entry.get("arguments"), list):
        if not all(isinstance(arg, str) for arg in entry["arguments"]):
            raise ValueError("arguments must be strings")
        return entry["arguments"]
    command = entry.get("command")
    if not isinstance(command, str):
        raise ValueError("missing command/arguments")
    windows = bool(re.match(r"^[A-Za-z]:[/\\]", entry.get("directory", "")))
    return split_command(command, windows)


def expand_responses(arguments, directory, problems, seen=None):
    seen = set() if seen is None else seen
    result = []
    windows = bool(re.match(r"^[A-Za-z]:[/\\]", normalized(directory)))
    for argument in arguments:
        if not argument.startswith("@"):
            result.append(argument)
            continue
        filename = argument[1:].strip('"')
        path = absolute_path(filename, directory)
        key = path_key(path)
        if key in seen or len(seen) >= 16:
            problems.append("Response file cycle/depth limit: {}".format(normalized(path)))
            continue
        try:
            contents = path.read_text(encoding="utf-8-sig")
            nested = split_command(contents, windows)
        except (OSError, ValueError) as error:
            problems.append("Cannot read response file {}: {}".format(normalized(path), error))
            continue
        result.extend(expand_responses(nested, directory, problems, seen | {key}))
    return result


def option_value(arguments, option):
    result = None
    for index, argument in enumerate(arguments):
        if argument == option and index + 1 < len(arguments):
            result = arguments[index + 1]
        elif argument.startswith(option) and argument != option:
            result = argument[len(option):]
    return result


def language_for(source, arguments):
    language = option_value(arguments, "-x")
    if language:
        if language in ("c", "c-header", "cpp-output"):
            return "C"
        if language in ("c++", "c++-header", "c++-cpp-output"):
            return "CXX"
        if language != "none":
            return "ASM" if "assembler" in language else "OTHER"
    suffix = Path(normalized(source)).suffix
    if suffix == ".C" or suffix.lower() in (".cpp", ".cc", ".cxx", ".c++"):
        return "CXX"
    if suffix == ".c":
        return "C"
    return "ASM" if suffix.lower() in (".s", ".asm") else "OTHER"


def compiler_from(arguments, language):
    names = ("g++", "gcc", "clang++", "clang", "cc", "c++")
    for argument in arguments:
        basename = normalized(argument).rsplit("/", 1)[-1].removesuffix(".exe")
        if any(basename == name or basename.endswith("-" + name) for name in names):
            return normalized(argument)
    return None


def codegen_profile(arguments, project_path=None, idf_path=None):
    """Retain settings relevant to frame sizes; preserve their effective order."""
    prefixes = ("-m", "-std=", "-f")
    optimization = None
    other = []
    for argument in arguments:
        if re.fullmatch(r"-O(?:[0-3sgz]|fast)?", argument):
            optimization = argument
        elif argument.startswith(prefixes):
            if argument in ("-fstack-usage", "-fno-stack-usage") or argument.startswith(("-fcallgraph-info", "-fdiagnostics-", "-fmessage-length=")):
                continue
            if project_path is not None and idf_path is not None and re.match(r"-f(?:macro|file|debug|profile)-prefix-map=", argument):
                option, mapping = argument.split("=", 1)
                if "=" in mapping:
                    original, replacement = mapping.split("=", 1)
                    mapped = source_name(absolute_path(original, project_path), project_path, idf_path)
                    argument = option + "=" + mapped + "=" + replacement
            other.append(argument)
    return {"optimization": optimization or "-O0 (implicit)", "codegen_options": other}


def parse_su_line(line):
    # Split from the right: C++ signatures can contain whitespace and tabs.
    fields = line.rstrip("\r\n").rsplit("\t", 2)
    if len(fields) != 3 or not re.fullmatch(r"\d+", fields[1]):
        raise ValueError("expected location/function<TAB>bytes<TAB>qualifiers")
    location = re.fullmatch(r"(.*?):(\d+):(\d+):(.*)", fields[0])
    if not location or not location[4]:
        raise ValueError("expected file:line:column:function location")
    qualifiers = sorted(set(filter(None, re.split(r"[,\s]+", fields[2].strip()))))
    if qualifiers == ["static"]:
        kind = "static"
    elif qualifiers == ["bounded", "dynamic"]:
        kind = "dynamic_bounded"
    elif qualifiers == ["dynamic"]:
        kind = "dynamic_unbounded"
    else:
        kind = "unknown"
    return {"source": normalized(location[1]), "line": int(location[2]), "column": int(location[3]),
            "function": location[4], "bytes": int(fields[1]), "qualifiers": qualifiers, "kind": kind}


def read_config(build_dir, description, warnings):
    config_path = build_dir / "config" / "sdkconfig.json"
    if config_path.exists():
        config = read_json(config_path)
        if not isinstance(config, dict):
            raise ReportError("{} must contain an object".format(config_path))
        return config
    filename = description.get("config_file")
    if filename:
        path = absolute_path(filename, build_dir)
        try:
            values = {}
            for line in path.read_text(encoding="utf-8-sig").splitlines():
                match = re.match(r"CONFIG_(\w+)=(.*)", line)
                if match:
                    value = match[2]
                    values[match[1]] = True if value == "y" else value
            return values
        except OSError as error:
            warnings.append("Cannot read sdkconfig {}: {}".format(normalized(path), error))
    warnings.append("sdkconfig is unavailable; optimization profile is based on compile commands only.")
    return {}


def compiler_identity(build_dir, language, executables):
    records = []
    compiler_file = "CMake{}Compiler.cmake".format(language)
    for path in sorted((build_dir / "CMakeFiles").glob("*/" + compiler_file)):
        try:
            contents = path.read_text(encoding="utf-8-sig")
        except OSError:
            continue
        fields = {}
        for name in ("COMPILER", "COMPILER_ID", "COMPILER_VERSION"):
            match = re.search(r'set\(CMAKE_' + language + "_" + name + r'\s+"([^"\r\n]*)"\)', contents)
            fields[name.lower()] = normalized(match[1]) if match else None
        if fields["compiler"] and any(path_key(fields["compiler"]) == path_key(executable) for executable in executables):
            records.append(fields)
    identities = []
    for executable in sorted(executables):
        matched = [record for record in records if path_key(record["compiler"]) == path_key(executable)]
        versions = {record["compiler_version"] for record in matched if record["compiler_version"]}
        ids = {record["compiler_id"] for record in matched if record["compiler_id"]}
        identities.append({"path": executable, "id": next(iter(ids)) if len(ids) == 1 else None,
                           "version": next(iter(versions)) if len(versions) == 1 else None})
    return identities


def describe_build(build_dir, description, config, compilers, profiles, units, response_problems):
    compiler_records = {language: compiler_identity(build_dir, language, paths)
                        for language, paths in sorted(compilers.items())}
    config_profile = {key: value for key, value in sorted(config.items())
                      if (key.startswith("COMPILER_") or key in ("LIBC_NEWLIB", "LIBC_PICOLIBC", "FREERTOS_SMP"))
                      and "OPTIMIZATION_ASSERTION" not in key}
    profile = {"sdkconfig": config_profile,
               "compile_profiles": sorted(profiles.values(), key=lambda item: json.dumps(item, sort_keys=True)),
               "translation_units": [{key: unit.get(key) for key in ("source", "object", "language", "compiler", "compile_profile")}
                                     for unit in units],
               "response_files_complete": not response_problems}
    identity = {"project": description.get("project_name"), "project_version": description.get("project_version"),
                "target": description.get("target"), "idf_version": description.get("git_revision"),
                "compilers": compiler_records, "profile": profile}
    compatibility = {key: identity[key] for key in ("project", "target", "idf_version", "compilers")}
    # File additions/removals are ordinary development changes. Keep the
    # global identity separate from TU assignments; compare assignments only
    # for common TUs below, never treating a new TU as a compatible old one.
    compatibility["profile"] = {key: profile[key] for key in ("sdkconfig", "response_files_complete")}
    known = (bool(identity["project"] and identity["target"] and identity["idf_version"] and compiler_records)
             and not response_problems
             and all(unit.get("compiler") for unit in units)
             and all(records and all(item["id"] and item["version"] for item in records)
                     for records in compiler_records.values()))
    identity["comparison_profile_complete"] = known
    identity["comparison_key"] = digest(compatibility)
    return identity


def collect_report(build_dir, frame_warning_bytes=1024):
    build_dir = Path(build_dir).resolve()
    description = read_json(build_dir / "project_description.json")
    database = read_json(build_dir / "compile_commands.json")
    if not isinstance(description, dict) or not isinstance(database, list):
        raise ReportError("project_description.json must be an object and compile_commands.json must be an array")
    warnings, response_problems, units, frames = [], [], [], []
    ignored = Counter()
    compilers, profiles = {}, {}
    component_names = description.get("build_components")
    active_components = set(component_names) if isinstance(component_names, list) else None
    project_path = absolute_path(description.get("project_path", str(build_dir.parent)), build_dir)
    idf_path = absolute_path(description.get("idf_path", ""), build_dir)
    seen_outputs = set()
    for entry_number, entry in enumerate(database, 1):
        if not isinstance(entry, dict) or not isinstance(entry.get("file"), str):
            warnings.append("Invalid compile database entry {}: missing source file".format(entry_number))
            ignored["invalid_entries"] += 1
            continue
        directory = absolute_path(entry.get("directory", str(build_dir)), build_dir)
        source = absolute_path(entry["file"], directory)
        unit_source = source_name(source, project_path, idf_path)
        try:
            arguments = command_arguments(entry)
            local_problems = []
            arguments = expand_responses(arguments, directory, local_problems)
            response_problems.extend(local_problems)
        except ValueError as error:
            warnings.append("Invalid command for {}: {}".format(unit_source, error))
            units.append({"source": unit_source, "status": "invalid_command", "problem": str(error)})
            continue
        language = language_for(source, arguments)
        if language not in ("C", "CXX"):
            ignored["assembly_translation_units" if language == "ASM" else "other_translation_units"] += 1
            continue
        output = entry.get("output") or option_value(arguments, "-o")
        unit = {"source": unit_source, "language": language}
        if not output:
            unit.update(status="unknown_output", problem="No output/-o in compile database entry")
            units.append(unit)
            continue
        object_path = absolute_path(output, directory)
        object_name = relative_name(object_path, build_dir)
        if object_name.startswith("bootloader/"):
            ignored["bootloader_translation_units"] += 1
            continue
        component = re.match(r"esp-idf/([^/]+)/", object_name)
        component = component[1] if component else "(application)"
        if active_components is not None and component != "(application)" and component not in active_components:
            ignored["inactive_component_translation_units"] += 1
            continue
        output_key = path_key(object_path)
        if output_key in seen_outputs:
            ignored["duplicate_compile_entries"] += 1
            continue
        seen_outputs.add(output_key)
        unit.update(component=component, object=object_name)
        compiler = compiler_from(arguments, language)
        if compiler:
            compilers.setdefault(language, set()).add(compiler)
        else:
            warnings.append("Cannot identify {} compiler for {}".format(language, unit_source))
        profile = codegen_profile(arguments, project_path, idf_path)
        unit.update(compiler=compiler, compile_profile=profile)
        profiles[digest({"language": language, **profile})] = {"language": language, **profile}
        stack_usage = False
        for argument in arguments:
            if argument in ("-fstack-usage", "-fno-stack-usage"):
                stack_usage = argument == "-fstack-usage"
        if not stack_usage:
            unit.update(status="stack_usage_disabled", problem="Current compile command does not enable -fstack-usage")
        elif any(arg.startswith(("-dumpdir", "-dumpbase", "-save-temps")) for arg in arguments):
            unit.update(status="unsupported_dump_options", problem="Custom dump/save-temps naming prevents reliable .su mapping")
        elif not object_path.is_file():
            unit.update(status="missing_object", problem="Current compile output is absent; build the application first")
        else:
            # GCC replaces only the output's final object suffix: foo.cpp.obj
            # becomes foo.cpp.su, not foo.su or foo.cpp.obj.su.
            su_path = object_path.with_suffix(".su")
            unit["metadata"] = relative_name(su_path, build_dir)
            if not su_path.is_file():
                unit.update(status="missing_metadata", problem="Expected .su file is absent (including possible compiler-cache sidecar omission)")
            else:
                try:
                    lines = su_path.read_text(encoding="utf-8-sig", errors="strict").splitlines()
                except (OSError, UnicodeError) as error:
                    unit.update(status="unreadable_metadata", problem=str(error))
                else:
                    errors, count = [], 0
                    for number, line in enumerate(lines, 1):
                        if not line.strip():
                            continue
                        try:
                            frame = parse_su_line(line)
                        except ValueError as error:
                            errors.append({"line": number, "problem": str(error), "text": line})
                            continue
                        location_source = frame["source"]
                        if location_source.startswith("<"):
                            frame["source"] = location_source
                        else:
                            location_path = absolute_path(location_source, directory)
                            frame["source"] = source_name(location_path, project_path, idf_path)
                        frame.update(translation_unit=unit_source, object=object_name, component=component,
                                     exceeds_frame_warning=frame["bytes"] > frame_warning_bytes)
                        frame["id"] = digest([object_name, frame["source"], frame["function"]])
                        frames.append(frame)
                        count += 1
                    unit.update(status="parse_errors" if errors else "reported", frames=count)
                    if errors:
                        unit["errors"] = errors
        units.append(unit)
    units.sort(key=lambda unit: (unit.get("object", ""), unit["source"]))
    frames.sort(key=lambda frame: (-frame["bytes"], frame["function"], frame["source"], frame["object"], frame["line"], frame["column"]))
    config = read_config(build_dir, description, warnings)
    identity = describe_build(build_dir, description, config, compilers, profiles, units, response_problems)
    if not identity["comparison_profile_complete"]:
        warnings.append("Build/compiler profile is incomplete; automatic frame-growth comparison is disabled.")
    warnings.extend(sorted(set(response_problems)))
    kinds = Counter(frame["kind"] for frame in frames)
    statuses = Counter(unit["status"] for unit in units)
    summary = {"expected_translation_units": len(units), "reported_translation_units": statuses["reported"],
               "metadata_translation_units": statuses["reported"] + statuses["parse_errors"],
               "translation_unit_statuses": dict(sorted(statuses.items())), "ignored": dict(sorted(ignored.items())),
               "frames": len(frames), "static_frames": kinds["static"],
               "dynamic_frames": kinds["dynamic_bounded"] + kinds["dynamic_unbounded"],
               "bounded_dynamic_frames": kinds["dynamic_bounded"], "unbounded_dynamic_frames": kinds["dynamic_unbounded"],
               "unknown_qualifier_frames": kinds["unknown"],
               "frames_exceeding_warning": sum(frame["exceeds_frame_warning"] for frame in frames),
               "source_metadata_complete": bool(units) and statuses["reported"] == len(units) and not response_problems and not ignored["invalid_entries"] and not kinds["unknown"],
               "whole_program_coverage": "incomplete"}
    elf_name = description.get("app_elf")
    elf_path = absolute_path(elf_name, build_dir) if elf_name else None
    elf_hash = file_digest(elf_path) if elf_path and elf_path.is_file() else None
    if not elf_hash:
        warnings.append("Application ELF unavailable; build fingerprint uses report metadata only.")
    report = {"schema_version": SCHEMA_VERSION, "identity": identity,
              "artifact": {"elf": relative_name(elf_path, build_dir) if elf_path else None, "elf_sha256": elf_hash},
              "settings": {"frame_warning_bytes": frame_warning_bytes}, "summary": summary,
              "translation_units": units, "frames": frames, "limitations": LIMITATIONS, "warnings": sorted(set(warnings))}
    report["artifact"]["fingerprint"] = digest({"identity": identity["comparison_key"], "elf": elf_hash,
                                                "units": units, "frames": [{key: value for key, value in frame.items()
                                                                            if key != "exceeds_frame_warning"} for frame in frames]})
    return report


def compare_reports(current, baseline, baseline_label):
    result = {"status": "unavailable", "baseline": baseline_label, "reason": "No prior report", "growth": []}
    if not baseline:
        return result
    if baseline.get("schema_version") != SCHEMA_VERSION:
        result["reason"] = "Report schema differs"
        return result
    old_identity, identity = baseline.get("identity", {}), current["identity"]
    if not isinstance(old_identity, dict):
        result["reason"] = "Baseline build identity is malformed"
        return result
    if not identity.get("comparison_profile_complete") or not old_identity.get("comparison_profile_complete"):
        result["reason"] = "Target/compiler/build profile is incomplete"
        return result
    if identity["comparison_key"] != old_identity.get("comparison_key"):
        result["status"] = "incompatible"
        result["reason"] = "Project, target, IDF, compiler, or optimization/code-generation profile differs"
        return result
    old_tus = old_identity.get("profile", {}).get("translation_units")
    new_tus = identity["profile"]["translation_units"]
    if not isinstance(old_tus, list) or not all(isinstance(unit, dict) for unit in old_tus):
        result["reason"] = "Baseline translation-unit profiles are unavailable"
        return result
    def tu_key(unit):
        return (unit.get("object"), unit.get("source"))
    old_tus_by_key = {tu_key(unit): unit for unit in old_tus}
    new_tus_by_key = {tu_key(unit): unit for unit in new_tus}
    common_tus = old_tus_by_key.keys() & new_tus_by_key.keys()
    changed_profiles = sorted(key for key in common_tus if old_tus_by_key[key] != new_tus_by_key[key])
    if changed_profiles:
        result.update(status="incompatible", reason="Compiler/code-generation profile changed for common translation units",
                      mismatched_translation_units=[{"object": key[0], "source": key[1]} for key in changed_profiles])
        return result
    old_frames = baseline.get("frames")
    if not isinstance(old_frames, list):
        result["reason"] = "Baseline has no frame list"
        return result
    if not all(isinstance(frame, dict) and isinstance(frame.get("id"), str)
               and isinstance(frame.get("bytes"), int) and isinstance(frame.get("kind"), str) for frame in old_frames):
        result["reason"] = "Baseline frame list is malformed"
        return result
    old_counts = Counter(frame.get("id") for frame in old_frames)
    new_counts = Counter(frame["id"] for frame in current["frames"])
    old_by_id = {frame.get("id"): frame for frame in old_frames if old_counts[frame.get("id")] == 1}
    matched, grown, reduced, changed_kind = 0, [], 0, 0
    for frame in current["frames"]:
        if (frame["object"], frame["translation_unit"]) not in common_tus:
            continue
        old = old_by_id.get(frame["id"])
        if old is None or new_counts[frame["id"]] != 1:
            continue
        if frame["kind"] != old.get("kind"):
            changed_kind += 1
            continue
        if not isinstance(old.get("bytes"), int):
            continue
        matched += 1
        delta = frame["bytes"] - old["bytes"]
        if delta > 0:
            grown.append({"id": frame["id"], "function": frame["function"], "source": frame["source"],
                          "line": frame["line"], "object": frame["object"], "kind": frame["kind"],
                          "previous_bytes": old["bytes"], "bytes": frame["bytes"], "delta_bytes": delta})
        elif delta < 0:
            reduced += 1
    grown.sort(key=lambda frame: (-frame["delta_bytes"], -frame["bytes"], frame["function"], frame["object"]))
    result.update(status="compared", reason="Matched function frames only; no task stack bounds inferred",
                  baseline_artifact=baseline.get("artifact", {}), baseline_project_version=old_identity.get("project_version"),
                  matched_frames=matched, grown_frames=len(grown), reduced_frames=reduced,
                  changed_kind_frames=changed_kind, unmatched_frames=len(current["frames"]) - matched - changed_kind,
                  common_translation_units=len(common_tus),
                  added_translation_units=len(new_tus_by_key.keys() - old_tus_by_key.keys()),
                  removed_translation_units=len(old_tus_by_key.keys() - new_tus_by_key.keys()),
                  removed_frames=sum((frame.get("object"), frame.get("translation_unit")) not in new_tus_by_key
                                     or frame.get("id") not in new_counts for frame in old_frames),
                  growth=grown)
    return result


def render_text(report, top=50):
    identity, summary = report["identity"], report["summary"]
    profile = identity["profile"]
    lines = ["ESP32M GCC function stack frame report", "",
             "Project: {} {} | target: {} | ESP-IDF: {}".format(identity["project"], identity["project_version"], identity["target"], identity["idf_version"])]
    for language, compilers in identity["compilers"].items():
        for compiler in compilers:
            lines.append("{} compiler: {} {} ({})".format(language, compiler["id"] or "unknown", compiler["version"] or "unknown", compiler["path"]))
    lines.extend(["Optimization profiles: " + ", ".join(sorted(set(item["optimization"] for item in profile["compile_profiles"]))),
                  "ELF SHA-256: " + (report["artifact"]["elf_sha256"] or "unavailable"), "",
                  "C/C++ translation units: {}/{} reported; {} have metadata".format(summary["reported_translation_units"], summary["expected_translation_units"], summary["metadata_translation_units"]),
                  "Function frames: {} static; {} dynamic bounded; {} dynamic UNBOUNDED; {} unknown qualifiers".format(summary["static_frames"], summary["bounded_dynamic_frames"], summary["unbounded_dynamic_frames"], summary["unknown_qualifier_frames"]),
                  "Advisory frame threshold: {} bytes; {} frames exceed it".format(report["settings"]["frame_warning_bytes"], summary["frames_exceeding_warning"]),
                  "Whole-program coverage: INCOMPLETE", "",
                  "PER-TASK USAGE/RECOMMENDATIONS: UNAVAILABLE from function frame data.",
                  "These bytes exclude callees. Unbounded dynamic bytes are only the known portion.", ""])
    comparison = report["comparison"]
    lines.append("Frame comparison: {} - {}".format(comparison["status"], comparison["reason"]))
    if comparison["status"] == "compared":
        lines.append("Baseline: {} (version {})".format(comparison["baseline"], comparison.get("baseline_project_version")))
        lines.append("{} matched; {} grew; {} shrank; {} changed qualifier kind; {} unmatched".format(
            comparison["matched_frames"], comparison["grown_frames"], comparison["reduced_frames"], comparison["changed_kind_frames"], comparison["unmatched_frames"]))
        lines.append("Translation units: {} common; {} added; {} removed. Removed function frames: {}".format(
            comparison.get("common_translation_units", 0), comparison.get("added_translation_units", 0),
            comparison.get("removed_translation_units", 0), comparison.get("removed_frames", 0)))
        for frame in comparison["growth"][:top]:
            lines.append("  +{} B ({} -> {}) [{}] {} | {}:{}".format(frame["delta_bytes"], frame["previous_bytes"], frame["bytes"], frame["kind"], frame["function"].replace("\t", " "), frame["source"], frame["line"]))
    lines.extend(["", "Largest reported function frames ({} of {}):".format(min(top, len(report["frames"])), len(report["frames"])),
                  "  bytes  kind                function | source | object"])
    for frame in report["frames"][:top]:
        marker = " !" if frame["exceeds_frame_warning"] or frame["kind"] in ("dynamic_unbounded", "unknown") else ""
        lines.append("  {:>6}  {:<19} {}{} | {}:{}:{} | {}".format(frame["bytes"], frame["kind"], frame["function"].replace("\t", " "), marker, frame["source"], frame["line"], frame["column"], frame["object"]))
    own_frames = [frame for frame in report["frames"] if frame["component"] in ("esp32m", "main", "(application)")]
    own_limit = min(top, 20)
    lines.extend(["", "Largest esp32m/application function frames ({} of {}):".format(min(own_limit, len(own_frames)), len(own_frames)),
                  "  bytes  kind                component | function | source"])
    for frame in own_frames[:own_limit]:
        lines.append("  {:>6}  {:<19} {} | {} | {}:{}:{}".format(frame["bytes"], frame["kind"], frame["component"], frame["function"].replace("\t", " "), frame["source"], frame["line"], frame["column"]))
    gaps = [unit for unit in report["translation_units"] if unit["status"] != "reported"]
    if gaps:
        lines.extend(["", "Missing/unusable source metadata:"])
        for unit in gaps:
            lines.append("  {}: {} ({})".format(unit["status"], unit["source"], unit.get("problem", "malformed .su records")))
            for error in unit.get("errors", []):
                lines.append("    .su line {}: {}".format(error["line"], error["problem"]))
    lines.extend(["", "Limits:"] + ["  - " + limit for limit in report["limitations"]])
    if report["warnings"]:
        lines.extend(["", "Warnings:"] + ["  - " + warning for warning in report["warnings"]])
    return "\n".join(lines) + "\n"


def write_atomic(path, contents):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_name = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="\n", dir=path.parent, prefix=path.name + ".", suffix=".tmp", delete=False) as stream:
            temp_name = stream.name
            stream.write(contents)
        os.replace(temp_name, path)
    except OSError as error:
        raise ReportError("Cannot write {}: {}".format(path, error)) from error
    finally:
        if temp_name and os.path.exists(temp_name):
            os.unlink(temp_name)


def generate(build_dir, output_prefix=None, baseline_path=None, frame_warning_bytes=1024, top=50):
    build_dir = Path(build_dir).resolve()
    prefix = Path(output_prefix).resolve() if output_prefix else build_dir / "stack-usage"
    json_path, text_path = Path(str(prefix) + ".json"), Path(str(prefix) + ".txt")
    prior = read_json(json_path, required=False)
    if prior is not None and not isinstance(prior, dict):
        raise ReportError("Prior report {} must be an object".format(json_path))
    current = collect_report(build_dir, frame_warning_bytes)
    previous_path = Path(str(prefix) + ".previous.json")
    if baseline_path:
        baseline = read_json(baseline_path)
        if not isinstance(baseline, dict):
            raise ReportError("Baseline must contain a report object")
        current["comparison"] = compare_reports(current, baseline, normalized(Path(baseline_path).resolve()))
    elif (prior and prior.get("schema_version") == SCHEMA_VERSION
          and isinstance(prior.get("artifact"), dict)
          and prior["artifact"].get("fingerprint") == current["artifact"]["fingerprint"]
          and isinstance(prior.get("comparison"), dict)):
        # A no-op build must not silently replace a useful growth comparison
        # with a comparison of the same image against itself.
        current["comparison"] = prior["comparison"]
    else:
        current["comparison"] = compare_reports(current, prior, previous_path.name if prior else None)
        if prior:
            write_atomic(previous_path, json.dumps(prior, indent=2, sort_keys=True, ensure_ascii=False) + "\n")
    write_atomic(text_path, render_text(current, top))
    write_atomic(json_path, json.dumps(current, indent=2, sort_keys=True, ensure_ascii=False) + "\n")
    return current, text_path, json_path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path, help="ESP-IDF application build directory")
    parser.add_argument("--output-prefix", type=Path, help="Output prefix (default: BUILD/stack-usage)")
    parser.add_argument("--baseline", type=Path, help="Explicit saved report to compare (otherwise previous distinct build)")
    parser.add_argument("--frame-warning-bytes", type=int, default=1024, help="Advisory own-function threshold (default: 1024)")
    parser.add_argument("--top", type=int, default=50, help="Number of largest/grown frames shown in text (JSON contains all)")
    args = parser.parse_args(argv)
    if args.frame_warning_bytes < 0 or args.top < 1:
        parser.error("--frame-warning-bytes must be nonnegative and --top must be positive")
    try:
        report, text_path, json_path = generate(args.build_dir, args.output_prefix, args.baseline, args.frame_warning_bytes, args.top)
    except ReportError as error:
        print("stack-report: error: {}".format(error), file=sys.stderr)
        return 2
    summary = report["summary"]
    print("Stack frames: {} functions; {}/{} C/C++ TUs reported; {} unbounded; {} above {} B. Reports: {} ; {}".format(
        summary["frames"], summary["reported_translation_units"], summary["expected_translation_units"], summary["unbounded_dynamic_frames"], summary["frames_exceeding_warning"], args.frame_warning_bytes, text_path, json_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
