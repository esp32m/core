# Function stack report host tests

From the repository root:

```powershell
python -B -m unittest discover -s core/esp32m/tests/stack-report -p "test_*.py" -v
```

The suite creates and removes private temporary fixture directories here. It
does not compile firmware, execute a compiler, or access a device. Fixtures
cover Windows/POSIX commands, response files, GCC stack qualifiers, current
object-to-sidecar mapping, excluded/stale components, incomplete metadata,
build-profile compatibility, growth comparisons, and repeated report generation.

Run the report against an existing application build:

```powershell
python core/esp32m/scripts/stack-report.py --build-dir projects/scratch/build
```

Outputs are `stack-usage.txt` and `stack-usage.json` in the build directory.
The JSON includes every parsed frame; text defaults to the largest 50 globally
and a separate largest 20 for the esp32m, main, and application targets.
`--top 100` changes the text limit; `--frame-warning-bytes 1024` sets an
advisory threshold for each function's own reported stack allocation. It does
not establish a safe task stack size and does not fail the build on large frames.
Missing metadata is reported as a coverage gap. Missing or invalid required
input files and output write failures return exit code 2.

By default, a distinct report/image is compared with the preceding report only
when project, target, ESP-IDF, compiler, and per-translation-unit compilation
profiles match. The preceding distinct report is retained as
`stack-usage.previous.json`. Re-running against the same image and frame data
keeps the existing comparison so a no-op build does not erase growth findings.
This automatic previous-build comparison does not designate an accepted release.

For an explicit baseline, save a report outside the active build output, then:

```powershell
python core/esp32m/scripts/stack-report.py --build-dir projects/scratch/build --baseline path/to/retained-release-stack-usage.json
```

`--output-prefix path/to/report` writes `path/to/report.txt` and `.json`.
Global target/compiler/SDK/profile changes disable comparison instead of
presenting unlike frames as a stack regression. Added or removed translation
units are allowed and counted separately. Each translation unit common to both
reports must retain the same compiler and code-generation profile; a changed
common profile disables comparison. Matching function identities ignore source
line shifts. Dynamic qualifier changes and ambiguous duplicate function
identities are not presented as byte growth.

The report enumerates source-compiled functions, including ones the linker may
discard. It excludes callees, complete task call chains, ROM, precompiled
libraries, assembly, bootloader and external builds. Compiler caches can omit
or retain `.su` sidecars; a fresh build with ccache disabled is appropriate when
their coverage or freshness is uncertain. No per-task minimum, maximum, or
recommended allocation is inferred from these per-function records.
